// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NFC_ANDROID_NFC_SYSTEM_LEVEL_SETTING_H_
#define CHROME_BROWSER_NFC_ANDROID_NFC_SYSTEM_LEVEL_SETTING_H_

#include "base/callback.h"
#include "base/macros.h"

namespace content {
class WebContents;
}

// This class determines whether NFC is enabled system-wide on the device.
class NfcSystemLevelSetting {
 public:
  virtual ~NfcSystemLevelSetting() {}

  // Returns true if the NFC system level setting can be enabled.
  virtual bool IsNfcAccessPossible() = 0;

  // Returns true if the NFC system level setting is enabled.
  virtual bool IsNfcSystemLevelSettingEnabled() = 0;

  // Triggers a prompt to ask the user to turn on the system NFC setting on
  // their device, and invokes callback when prompt is completed.
  //
  // The prompt will be triggered in the activity of the web contents.
  //
  // The callback is guaranteed to be called unless the user never replies to
  // the prompt dialog, which in practice happens very infrequently since the
  // dialog is modal.
  //
  // The callback may be invoked a long time after this method has returned.
  // If you need to access in the callback an object that is not owned by the
  // callback, you should ensure that the object has not been destroyed before
  // accessing it to prevent crashes, e.g. by using weak pointer semantics.
  virtual void PromptToEnableNfcSystemLevelSetting(
      content::WebContents* web_contents,
      base::OnceClosure prompt_completed_callback) = 0;
};

#endif  // CHROME_BROWSER_NFC_ANDROID_NFC_SYSTEM_LEVEL_SETTING_H_
