// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_VR_GVR_INSTALL_HELPER_H_
#define CHROME_BROWSER_ANDROID_VR_GVR_INSTALL_HELPER_H_

#include <memory>

#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/android/vr/vr_module_provider.h"
#include "chrome/browser/vr/service/xr_install_helper.h"
#include "chrome/browser/vr/vr_export.h"

namespace vr {

class VR_EXPORT GvrInstallHelper : public XrInstallHelper {
 public:
  GvrInstallHelper();
  ~GvrInstallHelper() override;

  GvrInstallHelper(const GvrInstallHelper&) = delete;
  GvrInstallHelper& operator=(const GvrInstallHelper&) = delete;

  void EnsureInstalled(int render_process_id,
                       int render_frame_id,
                       OnInstallFinishedCallback install_callback) override;

 private:
  void OnModuleInstalled(bool success);
  void RunInstallFinishedCallback(bool succeeded);

  OnInstallFinishedCallback install_finished_callback_;
  std::unique_ptr<VrModuleProvider> module_delegate_;

  base::WeakPtrFactory<GvrInstallHelper> weak_ptr_{this};
};

}  // namespace vr

#endif  // CHROME_BROWSER_ANDROID_VR_GVR_INSTALL_HELPER_H_
