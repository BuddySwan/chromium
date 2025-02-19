// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/updater/win/setup/uninstall.h"

#include <shlobj.h>
#include <windows.h>
#include <memory>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/stl_util.h"
#include "base/strings/string16.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/win/scoped_com_initializer.h"
#include "chrome/installer/util/install_service_work_item.h"
#include "chrome/installer/util/install_util.h"
#include "chrome/installer/util/work_item_list.h"
#include "chrome/updater/constants.h"
#include "chrome/updater/util.h"
#include "chrome/updater/win/constants.h"
#include "chrome/updater/win/setup/setup_util.h"
#include "chrome/updater/win/task_scheduler.h"

namespace updater {

void DeleteComServer(HKEY root) {
  InstallUtil::DeleteRegistryKey(root, GetComServerClsidRegistryPath(),
                                 WorkItem::kWow64Default);
}

void DeleteComService() {
  DCHECK(::IsUserAnAdmin());

  InstallUtil::DeleteRegistryKey(HKEY_LOCAL_MACHINE,
                                 GetComServiceClsidRegistryPath(),
                                 WorkItem::kWow64Default);
  InstallUtil::DeleteRegistryKey(HKEY_LOCAL_MACHINE,
                                 GetComServiceAppidRegistryPath(),
                                 WorkItem::kWow64Default);
  if (!installer::InstallServiceWorkItem::DeleteService(kWindowsServiceName))
    LOG(WARNING) << "DeleteService failed.";
}

void DeleteComInterfaces(HKEY root) {
  const base::string16 iid_reg_path = GetComIidRegistryPath();
  const base::string16 typelib_reg_path = GetComTypeLibRegistryPath();
  for (const auto& reg_path : {iid_reg_path, typelib_reg_path}) {
    InstallUtil::DeleteRegistryKey(root, reg_path, WorkItem::kWow64Default);
  }
}

// Reverses the changes made by setup. This is a best effort uninstall:
// 1. Deletes the scheduled task.
// 2. Deletes the Clients and ClientState keys.
// 3. Runs the uninstall script in the install directory of the updater.
// The execution of this function and the script race each other but the script
// loops and waits in between iterations trying to delete the install directory.
int Uninstall(bool is_machine) {
  VLOG(1) << __func__ << ", is_machine: " << is_machine;
  DCHECK(!is_machine || ::IsUserAnAdmin());
  HKEY key = is_machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;

  auto scoped_com_initializer =
      std::make_unique<base::win::ScopedCOMInitializer>(
          base::win::ScopedCOMInitializer::kMTA);

  updater::UnregisterUpdateAppsTask();

  std::unique_ptr<WorkItemList> uninstall_list(WorkItem::CreateWorkItemList());
  uninstall_list->AddDeleteRegKeyWorkItem(key, base::ASCIIToUTF16(UPDATER_KEY),
                                          WorkItem::kWow64Default);
  if (!uninstall_list->Do()) {
    LOG(ERROR) << "Failed to delete the registry keys.";
    uninstall_list->Rollback();
    return -1;
  }

  DeleteComInterfaces(key);
  if (is_machine)
    DeleteComService();
  DeleteComServer(key);

  base::FilePath product_dir;
  if (!GetProductDirectory(&product_dir)) {
    LOG(ERROR) << "GetProductDirectory failed.";
    return -1;
  }

  base::char16 cmd_path[MAX_PATH] = {0};
  auto size = ExpandEnvironmentStrings(L"%SystemRoot%\\System32\\cmd.exe",
                                       cmd_path, base::size(cmd_path));
  if (!size || size >= MAX_PATH)
    return -1;

  base::FilePath script_path = product_dir.AppendASCII(kUninstallScript);

  base::string16 cmdline = cmd_path;
  base::StringAppendF(&cmdline, L" /Q /C \"%ls\"", script_path.value().c_str());
  base::LaunchOptions options;
  options.start_hidden = true;

  VLOG(1) << "Running " << cmdline;

  auto process = base::LaunchProcess(cmdline, options);
  if (!process.IsValid()) {
    LOG(ERROR) << "Failed to create process " << cmdline;
    return -1;
  }

  return 0;
}

}  // namespace updater
