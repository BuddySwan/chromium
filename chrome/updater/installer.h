// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_UPDATER_INSTALLER_H_
#define CHROME_UPDATER_INSTALLER_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/callback_forward.h"
#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/values.h"
#include "base/version.h"
#include "components/update_client/update_client.h"

namespace updater {

class Installer final : public update_client::CrxInstaller {
 public:
  struct InstallInfo {
    InstallInfo();
    ~InstallInfo();

    base::FilePath install_dir;
    base::Version version;
    std::string fingerprint;
    std::unique_ptr<base::DictionaryValue> manifest;

   private:
    DISALLOW_COPY_AND_ASSIGN(InstallInfo);
  };

  explicit Installer(const std::string& app_id);

  const std::string app_id() const { return app_id_; }

  // Returns the app ids that are managed by the CRX installer.
  static std::vector<std::string> FindAppIds();

  // Finds the highest version install of the app, and updates the install
  // info for this installer instance.
  void FindInstallOfApp();

  // Returns a CrxComponent instance that describes the current install
  // state of the app.
  update_client::CrxComponent MakeCrxComponent();

 private:
  ~Installer() override;

  // Overrides from update_client::CrxInstaller.
  void OnUpdateError(int error) override;
  void Install(const base::FilePath& unpack_path,
               const std::string& public_key,
               std::unique_ptr<InstallParams> install_params,
               Callback callback) override;
  bool GetInstalledFile(const std::string& file,
                        base::FilePath* installed_file) override;
  bool Uninstall() override;

  Result InstallHelper(const base::FilePath& unpack_path,
                       std::unique_ptr<InstallParams> install_params);

  // Runs the installer code with sync primitives to allow the code to
  // create processes and wait for them to exit.
  void InstallWithSyncPrimitives(const base::FilePath& unpack_path,
                                 const std::string& public_key,
                                 std::unique_ptr<InstallParams> install_params,
                                 Callback callback);

  // Handles the application installer specified by the |app_installer| and
  // its |arguments|. This data is returned by the update server as part of
  // the manifest object in an update response. Handling of the application
  // installer is typically OS-specific, such as building a command line,
  // creating processes, mounting images, running scripts, and collecting
  // exit codes.
  int RunApplicationInstaller(const base::FilePath& app_installer,
                              const std::string& arguments);

  const std::string app_id_;
  std::unique_ptr<InstallInfo> install_info_;

  DISALLOW_COPY_AND_ASSIGN(Installer);
};

}  // namespace updater

#endif  // CHROME_UPDATER_INSTALLER_H_
