# Copyright 2019 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Converts Chromium Test Expectations into WPT Metadata ini files.

This script loads TestExpectations for any WPT test and creates the metadata
files corresponding to the expectation. This script runs as a BUILD action rule.
The output is then bundled into the WPT isolate package to be shipped to bots
running the WPT test suite.
"""

import argparse
import logging
import os
import re

from blinkpy.common.system.log_utils import configure_logging
from blinkpy.web_tests.models import test_expectations
from blinkpy.web_tests.models.typ_types import ResultType
from collections import defaultdict

_log = logging.getLogger(__name__)

# Define some status bitmasks for combinations of statuses a test could exhibit.
# The test has a harness error in its baseline file.
HARNESS_ERROR = 1
# The test has at least one failing subtest in its baseline file.
SUBTEST_FAIL = 1 << 1
# The test should be skipped
SKIP_TEST = 1 << 2

# Next status: 1 << 3


class WPTMetadataBuilder(object):
    def __init__(self, expectations, port):
        """
        Args:
            expectations: a blinkpy.web_tests.models.test_expectations.TestExpectations object
            port: a blinkpy.web_tests.port.Port object
        """
        self.expectations = expectations
        self.port = port
        self.wpt_manifest = self.port.wpt_manifest("external/wpt")
        self.metadata_output_dir = ""

    def run(self, args=None):
        """Main entry point to parse flags and execute the script."""
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument("--metadata-output-dir",
                            help="The directory to output the metadata files into.")
        parser.add_argument('-v', '--verbose', action='store_true', help='More verbose logging.')
        args = parser.parse_args(args)

        log_level = logging.DEBUG if args.verbose else logging.INFO
        configure_logging(logging_level=log_level, include_time=True)

        self.metadata_output_dir = args.metadata_output_dir
        self._build_metadata_and_write()

        return 0

    def _build_metadata_and_write(self):
        """Build the metadata files and write them to disk."""
        if os.path.exists(self.metadata_output_dir):
            _log.debug("Output dir exists, deleting: %s",
                       self.metadata_output_dir)
            import shutil
            shutil.rmtree(self.metadata_output_dir)

        tests_for_metadata = self.get_tests_needing_metadata()
        _log.info("Found %d tests requiring metadata", len(tests_for_metadata))
        for test_name, test_status_bitmap in tests_for_metadata.items():
            filename, file_contents = self.get_metadata_filename_and_contents(test_name, test_status_bitmap)
            if not filename or not file_contents:
                continue
            self._write_to_file(filename, file_contents)

        # Finally, output a stamp file with the same name as the output
        # directory. The stamp file is empty, it's only used for its mtime.
        # This makes the GN build system happy (see crbug.com/995112).
        with open(self.metadata_output_dir + ".stamp", "w"):
            pass

    def _write_to_file(self, filename, file_contents):
        # Write the contents to the file name
        if not os.path.exists(os.path.dirname(filename)):
            os.makedirs(os.path.dirname(filename))
        # Note that we append to the metadata file in order to allow multiple
        # tests to be present in the same .ini file (ie: for multi-global tests)
        with open(filename, "a") as metadata_file:
            metadata_file.write(file_contents)

    def get_tests_needing_metadata(self):
        """Determines which tests need metadata files.

        This function loops over the tests to be run and checks whether each test
        has an expectation (eg: in TestExpectations) and/or a baseline (ie:
        test-name-expected.txt). The existence of those things will determine
        the information that will be emitted into the tests's metadata file.

        Returns:
            A dict. The key is the string test name and the value is an integer
            bitmap of statuses for the test.
        """
        tests_needing_metadata = defaultdict(int)
        for test_name in self.port.tests(paths=["external/wpt"]):
            # First check for expectations. If a test is skipped then we do not
            # look for more statuses
            expectation_line = self.expectations.get_expectations(test_name)
            test_statuses = expectation_line.results
            self._handle_test_with_expectation(test_name, test_statuses, tests_needing_metadata)
            if self._test_was_skipped(test_name, tests_needing_metadata):
                # Do not consider other statuses if a test is skipped
                continue

            # Check if the test has a baseline
            test_baseline = self.port.expected_text(test_name)
            if not test_baseline:
                continue
            self._handle_test_with_baseline(test_name, test_baseline, tests_needing_metadata)
        return tests_needing_metadata

    def _handle_test_with_expectation(self, test_name, test_statuses, status_dict):
        """Handles a single test expectation and updates |status_dict|."""
        # TODO(lpz): This will handle more statuses in the future, such as flakes.
        if ResultType.Skip in test_statuses:
            status_dict[test_name] |= SKIP_TEST

    def _test_was_skipped(self, test_name, status_dict):
        """Returns whether |test_name| is marked as skipped in |status_dict|."""
        return test_name in status_dict and (status_dict[test_name] & SKIP_TEST)

    def _handle_test_with_baseline(self, test_name, test_baseline, status_dict):
        """Handles a single test baseline and updates |status_dict|."""
        status_bitmap = 0
        if re.search(r"^(FAIL|NOTRUN|TIMEOUT)", test_baseline, re.MULTILINE):
            status_bitmap |= SUBTEST_FAIL
        if re.search(r"^Harness Error\.", test_baseline, re.MULTILINE):
            status_bitmap |= HARNESS_ERROR
        if status_bitmap > 0:
            status_dict[test_name] |= status_bitmap
        else:
            # Treat this as an error because we don't want it to happen.
            # Either the non-FAIL statuses need to be handled here, or the
            # baseline is all PASS which should just be deleted.
            _log.error("Test %s has a non-FAIL baseline" % test_name)

    def get_metadata_filename_and_contents(self, chromium_test_name, test_status_bitmap=0):
        """Determines the metadata filename and contents for the specified test.

        The metadata filename is derived from the test name but will differ if
        the expectation is for a single test or for a directory of tests. The
        contents of the metadata file will also differ for those two cases.

        Args:
            chromium_test_name: A Chromium test name from the expectation file,
                which starts with `external/wpt`.
            test_status_bitmap: An integer containing additional data about the
                status, such as enumerating flaky statuses, or whether a test has
                a combination of harness error and subtest failure.

        Returns:
            A pair of strings, the first is the path to the metadata file and
            the second is the contents to write to that file. Or None if the
            test does not need a metadata file.
        """
        # Ignore expectations for non-WPT tests
        if not chromium_test_name or not chromium_test_name.startswith('external/wpt'):
            return None, None

        # Split the test name by directory. We omit the first 2 entries because
        # they are 'external' and 'wpt' and these don't exist in the WPT's test
        # names.
        wpt_test_name_parts = chromium_test_name.split("/")[2:]
        # The WPT test name differs from the Chromium test name in that the WPT
        # name omits `external/wpt`.
        wpt_test_name = "/".join(wpt_test_name_parts)

        # Check if this is a test file or a test directory
        is_test_dir = chromium_test_name.endswith("/")
        metadata_filename = None
        metadata_file_contents = None
        if is_test_dir:
            # A test directory gets one metadata file called __dir__.ini and all
            # tests in that dir are skipped.
            metadata_filename = os.path.join(self.metadata_output_dir,
                                             *wpt_test_name_parts)
            metadata_filename = os.path.join(metadata_filename, "__dir__.ini")
            _log.debug("Creating a dir-wide ini file %s", metadata_filename)

            metadata_file_contents = self._get_dir_disabled_string()
        else:
            # For individual tests, we create one file per test, with the name
            # of the test in the file as well. This name can contain variants.
            test_file_path = self.wpt_manifest.file_path_for_test_url(wpt_test_name)
            if not test_file_path:
                _log.info("Could not find file for test %s, skipping" % wpt_test_name)
                return None, None
            test_file_parts = test_file_path.split("/")

            # Append `.ini` to the test filename to indicate it's the metadata
            # file. The `test_filename` can differ from the `wpt_test_name` for
            # multi-global tests.
            test_file_parts[-1] += ".ini"
            metadata_filename = os.path.join(self.metadata_output_dir,
                                             *test_file_parts)
            _log.debug("Creating a test ini file %s with status_bitmap %s", metadata_filename, test_status_bitmap)

            # The contents of the metadata file is two lines:
            # 1. the last part of the WPT test path (ie the filename) inside
            #    square brackets - this could differ from the metadata filename.
            # 2. an indented line with the test status and reason
            wpt_test_file_name_part = wpt_test_name_parts[-1]
            metadata_file_contents = self._get_test_failed_string(wpt_test_file_name_part, test_status_bitmap)

        return metadata_filename, metadata_file_contents

    def _get_dir_disabled_string(self):
        return "disabled: wpt_metadata_builder.py\n"

    def _get_test_disabled_string(self, test_name):
        return "[%s]\n  disabled: wpt_metadata_builder.py\n" % test_name

    def _get_test_failed_string(self, test_name, test_status_bitmap):
        result = "[%s]\n" % test_name

        # A skipped test is a little special in that it doesn't happen along with
        # any other status. So we compare directly against SKIP_TEST and also
        # return right away.
        if test_status_bitmap == SKIP_TEST:
            result += "  disabled: wpt_metadata_builder.py\n"
            return result

        # Other test statuses can exist together.
        if test_status_bitmap & HARNESS_ERROR:
            result += "  expected: ERROR\n"
        if test_status_bitmap & SUBTEST_FAIL:
            result += "  blink_expect_any_subtest_status: True # wpt_metadata_builder.py\n"
        return result
