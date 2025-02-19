// Copyright (c) 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/browser_watcher/exit_code_watcher_win.h"

#include <utility>

#include "base/logging.h"
#include "base/metrics/sparse_histogram.h"
#include "base/process/kill.h"
#include "base/process/process.h"
#include "base/process/process_handle.h"
#include "base/sequenced_task_runner.h"
#include "base/threading/thread.h"
#include "base/threading/thread_task_runner_handle.h"

#include <windows.h>

namespace browser_watcher {

const char kBrowserExitCodeHistogramName[] = "Stability.BrowserExitCodes";

ExitCodeWatcher::ExitCodeWatcher()
    : background_thread_("ExitCodeWatcherThread"), exit_code_(STILL_ACTIVE) {}

ExitCodeWatcher::~ExitCodeWatcher() {
}

bool ExitCodeWatcher::Initialize(base::Process process) {
  if (!process.IsValid()) {
    LOG(ERROR) << "Invalid parent handle, can't get parent process ID.";
    return false;
  }

  DWORD process_pid = process.Pid();
  if (process_pid == 0) {
    LOG(ERROR) << "Invalid parent handle, can't get parent process ID.";
    return false;
  }

  FILETIME creation_time = {};
  FILETIME dummy = {};
  if (!::GetProcessTimes(process.Handle(), &creation_time, &dummy, &dummy,
                         &dummy)) {
    PLOG(ERROR) << "Invalid parent handle, can't get parent process times.";
    return false;
  }

  // Success, take ownership of the process.
  process_ = std::move(process);

  return true;
}

bool ExitCodeWatcher::StartWatching() {
  if (!background_thread_.StartWithOptions(
          base::Thread::Options(base::MessagePumpType::IO, 0))) {
    return false;
  }

  if (!background_thread_.task_runner()->PostTask(
          FROM_HERE, base::BindOnce(&ExitCodeWatcher::WaitForExit,
                                    base::Unretained(this)))) {
    background_thread_.Stop();
    return false;
  }

  return true;
}

void ExitCodeWatcher::WaitForExit() {
  if (!process_.WaitForExit(&exit_code_)) {
    LOG(ERROR) << "Failed to wait for process.";
    return;
  }

  WriteProcessExitCode(exit_code_);
}

bool ExitCodeWatcher::WriteProcessExitCode(int exit_code) {
  if (exit_code != STILL_ACTIVE) {
    // Record the exit codes in a sparse stability histogram, as the range of
    // values used to report failures is large.
    base::HistogramBase* exit_code_histogram =
        base::SparseHistogram::FactoryGet(
            kBrowserExitCodeHistogramName,
            base::HistogramBase::kUmaStabilityHistogramFlag);
    exit_code_histogram->Add(exit_code);
    return true;
  }
  return false;
}

}  // namespace browser_watcher
