# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Test runner for running tests using xcodebuild."""

import collections
import distutils.version
import logging
from multiprocessing import pool
import os
import subprocess
import time

import iossim_util
import test_apps
import test_runner
import xcode_log_parser

LOGGER = logging.getLogger(__name__)
MAXIMUM_TESTS_PER_SHARD_FOR_RERUN = 20
XTDEVICE_FOLDER = os.path.expanduser('~/Library/Developer/XCTestDevices')


class LaunchCommandCreationError(test_runner.TestRunnerError):
  """One of launch command parameters was not set properly."""

  def __init__(self, message):
    super(LaunchCommandCreationError, self).__init__(message)


class LaunchCommandPoolCreationError(test_runner.TestRunnerError):
  """Failed to create a pool of launch commands."""

  def __init__(self, message):
    super(LaunchCommandPoolCreationError, self).__init__(message)


def get_all_tests(app_path, test_cases=None):
  """Gets all tests from test bundle."""
  test_app_bundle = os.path.join(app_path, os.path.splitext(
      os.path.basename(app_path))[0])
  # Method names that starts with test* and also are in *TestCase classes
  # but they are not test-methods.
  # TODO(crbug.com/982435): Rename not test methods with test-suffix.
  not_tests = ['ChromeTestCase/testServer', 'FindInPageTestCase/testURL']
  all_tests = []
  for test_class, test_method in test_runner.get_test_names(test_app_bundle):
    test_name = '%s/%s' % (test_class, test_method)
    if (test_name not in not_tests and
        # Filter by self.test_cases if specified
        (test_class in test_cases if test_cases else True)):
      all_tests.append(test_name)
  return all_tests


def erase_all_simulators(path=None):
  """Erases all simulator devices.

  Args:
    path: (str) A path with simulators

  Fix for DVTCoreSimulatorAdditionsErrorDomain error.
  """
  command = ['xcrun', 'simctl']
  if path:
    command += ['--set', path]
    LOGGER.info('Erasing all simulators from folder %s.' % path)
  else:
    LOGGER.info('Erasing all simulators.')
  subprocess.call(command + ['erase', 'all'])


def shutdown_all_simulators(path=None):
  """Shutdown all simulator devices.

  Args:
    path: (str) A path with simulators

  Fix for DVTCoreSimulatorAdditionsErrorDomain error.
  """
  command = ['xcrun', 'simctl']
  if path:
    command += ['--set', path]
    LOGGER.info('Shutdown all simulators from folder %s.' % path)
  else:
    LOGGER.info('Shutdown all simulators.')
  subprocess.call(command + ['shutdown', 'all'])


def terminate_process(proc):
  """Terminates the process.

  If an error occurs ignore it, just print out a message.

  Args:
    proc: A subprocess.
  """
  try:
    proc.terminate()
  except OSError as ex:
    LOGGER.info('Error while killing a process: %s' % ex)


class LaunchCommand(object):
  """Stores xcodebuild test launching command."""

  def __init__(self,
               egtests_app,
               udid,
               shards,
               retries,
               out_dir=os.path.basename(os.getcwd()),
               env=None):
    """Initialize launch command.

    Args:
      egtests_app: (EgtestsApp) An egtests_app to run.
      udid: (str) UDID of a device/simulator.
      shards: (int) A number of shards.
      retries: (int) A number of retries.
      out_dir: (str) A folder in which xcodebuild will generate test output.
        By default it is a current directory.
      env: (dict) Environment variables.

    Raises:
      LaunchCommandCreationError: if one of parameters was not set properly.
    """
    if not isinstance(egtests_app, test_apps.EgtestsApp):
      raise test_runner.AppNotFoundError(
          'Parameter `egtests_app` is not EgtestsApp: %s' % egtests_app)
    self.egtests_app = egtests_app
    self.udid = udid
    self.shards = shards
    self.retries = retries
    self.out_dir = out_dir
    self.logs = collections.OrderedDict()
    self.test_results = collections.OrderedDict()
    self.env = env
    if distutils.version.LooseVersion('11.0') <= distutils.version.LooseVersion(
        test_runner.get_current_xcode_info()['version']):
      self._log_parser = xcode_log_parser.Xcode11LogParser()
    else:
      self._log_parser = xcode_log_parser.XcodeLogParser()

  def summary_log(self):
    """Calculates test summary - how many passed, failed and error tests.

    Returns:
      Dictionary with number of passed and failed tests.
      Failed tests will be calculated from the last test attempt.
      Passed tests calculated for each test attempt.
    """
    test_statuses = ['passed', 'failed']
    for status in test_statuses:
      self.logs[status] = 0

    for index, test_attempt_results in enumerate(self.test_results['attempts']):
      for test_status in test_statuses:
        if test_status not in test_attempt_results:
          continue
        if (test_status == 'passed'
            # Number of failed tests is taken only from last run.
            or (test_status == 'failed'
                and index == len(self.test_results['attempts']) - 1)):
          self.logs[test_status] += len(test_attempt_results[test_status])

  def launch_attempt(self, cmd):
    """Launch a process and do logging simultaneously.

    Args:
      cmd: (list[str]) A command to run.

    Returns:
      output - command output as list of strings.
    """
    proc = subprocess.Popen(
        cmd,
        env=self.env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    return test_runner.print_process_output(proc)

  def launch(self):
    """Launches tests using xcodebuild."""
    self.test_results['attempts'] = []
    cancelled_statuses = {'TESTS_DID_NOT_START', 'BUILD_INTERRUPTED'}
    shards = self.shards
    running_tests = set(
        get_all_tests(self.egtests_app.test_app_path,
                      self.egtests_app.included_tests))
    # total number of attempts is self.retries+1
    for attempt in range(self.retries + 1):
      # Erase all simulators per each attempt
      if iossim_util.is_device_with_udid_simulator(self.udid):
        # kill all running simulators to prevent possible memory leaks
        test_runner.SimulatorTestRunner.kill_simulators()
        shutdown_all_simulators()
        shutdown_all_simulators(XTDEVICE_FOLDER)
        erase_all_simulators()
        erase_all_simulators(XTDEVICE_FOLDER)
      outdir_attempt = os.path.join(self.out_dir, 'attempt_%d' % attempt)
      cmd_list = self.egtests_app.command(outdir_attempt,
                                          'id=%s' % self.udid, shards)
      # TODO(crbug.com/914878): add heartbeat logging to xcodebuild_runner.
      LOGGER.info('Start test attempt #%d for command [%s]' % (
          attempt, ' '.join(cmd_list)))
      output = self.launch_attempt(cmd_list)
      self.test_results['attempts'].append(
          self._log_parser.collect_test_results(outdir_attempt, output))
      if self.retries == attempt or not self.test_results[
          'attempts'][-1]['failed']:
        break
      # Exclude passed tests in next test attempt.
      self.egtests_app.excluded_tests += self.test_results['attempts'][-1][
          'passed']
      # crbug.com/987664 - for the case when
      # all tests passed but build was interrupted,
      # excluded(passed) tests are equal to tests to run.
      if set(self.egtests_app.excluded_tests) == running_tests:
        for status in cancelled_statuses:
          failure = self.test_results['attempts'][-1]['failed'].pop(
              status, None)
          if failure:
            LOGGER.info('Failure for passed tests %s: %s' % (status, failure))
        break
      self._log_parser.copy_screenshots(outdir_attempt)
      # If tests are not completed(interrupted or did not start)
      # re-run them with the same number of shards,
      # otherwise re-run with shards=1 and exclude passed tests.
      cancelled_attempt = cancelled_statuses.intersection(
          self.test_results['attempts'][-1]['failed'].keys())
      if (not cancelled_attempt
          # If need to re-run less than 20 tests, 1 shard should be enough.
          or (len(running_tests) - len(self.egtests_app.excluded_tests)
              <= MAXIMUM_TESTS_PER_SHARD_FOR_RERUN)):
        shards = 1
    self.summary_log()

    return {
        'test_results': self.test_results,
        'logs': self.logs
    }


class SimulatorParallelTestRunner(test_runner.SimulatorTestRunner):
  """Class for running simulator tests using xCode."""

  def __init__(
      self,
      app_path,
      host_app_path,
      iossim_path,
      xcode_build_version,
      version,
      platform,
      out_dir,
      mac_toolchain=None,
      retries=1,
      shards=1,
      xcode_path=None,
      test_cases=None,
      test_args=None,
      env_vars=None
  ):
    """Initializes a new instance of SimulatorParallelTestRunner class.

    Args:
      app_path: (str) A path to egtests_app.
      host_app_path: (str) A path to the host app for EG2.
      iossim_path: Path to the compiled iossim binary to use.
                   Not used, but is required by the base class.
      xcode_build_version: (str) Xcode build version for running tests.
      version: (str) iOS version to run simulator on.
      platform: (str) Name of device.
      out_dir: (str) A directory to emit test data into.
      mac_toolchain: (str) A command to run `mac_toolchain` tool.
      retries: (int) A number to retry test run, will re-run only failed tests.
      shards: (int) A number of shards. Default is 1.
      xcode_path: (str) A path to Xcode.app folder.
      test_cases: (list) List of tests to be included in the test run.
                  None or [] to include all tests.
      test_args: List of strings to pass as arguments to the test when
        launching.
      env_vars: List of environment variables to pass to the test itself.

    Raises:
      AppNotFoundError: If the given app does not exist.
      PlugInsNotFoundError: If the PlugIns directory does not exist for XCTests.
      XcodeVersionNotFoundError: If the given Xcode version does not exist.
      XCTestPlugInNotFoundError: If the .xctest PlugIn does not exist.
    """
    super(SimulatorParallelTestRunner, self).__init__(
        app_path,
        iossim_path,
        platform,
        version,
        xcode_build_version,
        out_dir,
        env_vars=env_vars,
        mac_toolchain=mac_toolchain,
        retries=retries or 1,
        shards=shards or 1,
        test_args=test_args,
        test_cases=test_cases,
        xcode_path=xcode_path,
        xctest=False
    )
    self.set_up()
    self.host_app_path = None
    if host_app_path != 'NO_PATH':
      self.host_app_path = os.path.abspath(host_app_path)
    self._init_sharding_data()
    self.logs = collections.OrderedDict()
    self.test_results['path_delimiter'] = '/'

  def _init_sharding_data(self):
    """Initialize sharding data.

    For common case info about sharding tests will be a list of dictionaries:
    [
        {
            'app':paths to egtests_app,
            'udid': 'UDID of Simulator'
            'shards': N
        }
    ]
    """
    self.sharding_data = [{
        'app': self.app_path,
        'host': self.host_app_path,
        'udid': self.udid,
        'shards': self.shards,
        'test_cases': self.test_cases
    }]

  def get_launch_env(self):
    """Returns a dict of environment variables to use to launch the test app.

    Returns:
      A dict of environment variables.
    """
    env = super(test_runner.SimulatorTestRunner, self).get_launch_env()
    env['NSUnbufferedIO'] = 'YES'
    return env

  def launch(self):
    """Launches tests using xcodebuild."""
    launch_commands = []
    for params in self.sharding_data:
      test_app = test_apps.EgtestsApp(
          params['app'],
          included_tests=params['test_cases'],
          env_vars=self.env_vars,
          test_args=self.test_args,
          host_app_path=params['host'])
      launch_commands.append(
          LaunchCommand(
              test_app,
              udid=params['udid'],
              shards=params['shards'],
              retries=self.retries,
              out_dir=os.path.join(self.out_dir, params['udid']),
              env=self.get_launch_env()))

    thread_pool = pool.ThreadPool(len(launch_commands))
    attempts_results = []
    for result in thread_pool.imap_unordered(LaunchCommand.launch,
                                             launch_commands):
      attempts_results.append(result['test_results']['attempts'])

    # Gets passed tests
    self.logs['passed tests'] = []
    for shard_attempts in attempts_results:
      for attempt in shard_attempts:
        self.logs['passed tests'].extend(attempt['passed'])

    # If the last attempt does not have failures, mark failed as empty
    self.logs['failed tests'] = []
    for shard_attempts in attempts_results:
      if shard_attempts[-1]['failed']:
        self.logs['failed tests'].extend(shard_attempts[-1]['failed'].keys())

    # Gets all failures/flakes and lists them in bot summary
    all_failures = set()
    for shard_attempts in attempts_results:
      for attempt, attempt_results in enumerate(shard_attempts):
        for failure in attempt_results['failed']:
          if failure not in self.logs:
            self.logs[failure] = []
          self.logs[failure].append('%s: attempt # %d' % (failure, attempt))
          self.logs[failure].extend(attempt_results['failed'][failure])
          all_failures.add(failure)

    # Gets only flaky(not failed) tests.
    self.logs['flaked tests'] = list(
        all_failures - set(self.logs['failed tests']))

    # Gets not-started/interrupted tests
    all_tests_to_run = set(get_all_tests(self.app_path, self.test_cases))
    aborted_tests = list(all_tests_to_run - set(self.logs['failed tests']) -
                         set(self.logs['passed tests']))
    aborted_tests.sort()
    self.logs['aborted tests'] = aborted_tests

    self.test_results['interrupted'] = bool(aborted_tests)
    self.test_results['num_failures_by_type'] = {
        'FAIL': len(self.logs['failed tests'] + self.logs['aborted tests']),
        'PASS': len(self.logs['passed tests']),
    }
    self.test_results['tests'] = collections.OrderedDict()

    for shard_attempts in attempts_results:
      for attempt, attempt_results in enumerate(shard_attempts):

        for test in attempt_results['failed'].keys() + self.logs[
            'aborted tests']:
          if attempt == len(shard_attempts) - 1:
            test_result = 'FAIL'
          else:
            test_result = self.test_results['tests'].get(test, {}).get(
                'actual', '') + ' FAIL'
          self.test_results['tests'][test] = {
              'expected': 'PASS',
              'actual': test_result.strip()
          }

        for test in attempt_results['passed']:
          test_result = self.test_results['tests'].get(test, {}).get(
              'actual', '') + ' PASS'
          self.test_results['tests'][test] = {
              'expected': 'PASS',
              'actual': test_result.strip()
          }
          if 'FAIL' in test_result:
            self.test_results['tests'][test]['is_flaky'] = True

    # Test is failed if there are failures for the last run.
    return not self.logs['failed tests']


class DeviceXcodeTestRunner(SimulatorParallelTestRunner,
                            test_runner.DeviceTestRunner):
  """Class for running tests on real device using xCode."""

  def __init__(
      self,
      app_path,
      host_app_path,
      xcode_build_version,
      out_dir,
      mac_toolchain=None,
      retries=1,
      xcode_path=None,
      test_cases=None,
      test_args=None,
      env_vars=None,
  ):
    """Initializes a new instance of DeviceXcodeTestRunner class.

    Args:
      app_path: (str) A path to egtests_app.
      host_app_path: (str) A path to the host app for EG2.
      xcode_build_version: (str) Xcode build version for running tests.
      out_dir: (str) A directory to emit test data into.
      mac_toolchain: (str) A command to run `mac_toolchain` tool.
      retries: (int) A number to retry test run, will re-run only failed tests.
      xcode_path: (str) A path to Xcode.app folder.
      test_cases: (list) List of tests to be included in the test run.
                  None or [] to include all tests.
      test_args: List of strings to pass as arguments to the test when
        launching.
      env_vars: List of environment variables to pass to the test itself.

    Raises:
      AppNotFoundError: If the given app does not exist.
      DeviceDetectionError: If no device found.
      PlugInsNotFoundError: If the PlugIns directory does not exist for XCTests.
      XcodeVersionNotFoundError: If the given Xcode version does not exist.
      XCTestPlugInNotFoundError: If the .xctest PlugIn does not exist.
    """
    test_runner.DeviceTestRunner.__init__(
        self,
        app_path,
        xcode_build_version,
        out_dir,
        env_vars=env_vars,
        retries=retries,
        test_args=test_args,
        test_cases=test_cases,
        mac_toolchain=mac_toolchain,
        xcode_path=xcode_path,
    )
    self.shards = 1  # For tests on real devices shards=1
    self.version = None
    self.platform = None
    self.host_app_path = None
    if host_app_path != 'NO_PATH':
      self.host_app_path = os.path.abspath(host_app_path)
    self.homedir = ''
    self.set_up()
    self._init_sharding_data()
    self.start_time = time.strftime('%Y-%m-%d-%H%M%S', time.localtime())
    self.test_results['path_delimiter'] = '/'

  def set_up(self):
    """Performs setup actions which must occur prior to every test launch."""
    self.uninstall_apps()
    self.wipe_derived_data()

  def tear_down(self):
    """Performs cleanup actions which must occur after every test launch."""
    test_runner.DeviceTestRunner.tear_down(self)

  def launch(self):
    try:
      return super(DeviceXcodeTestRunner, self).launch()
    finally:
      self.tear_down()
