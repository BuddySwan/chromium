// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/web_view/internal/passwords/web_view_password_feature_manager.h"

#include "base/logging.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace ios_web_view {

bool WebViewPasswordFeatureManager::IsGenerationEnabled() const {
  return false;
}

bool WebViewPasswordFeatureManager::ShouldCheckReuseOnLeakDetection() const {
  return false;
}

bool WebViewPasswordFeatureManager::IsOptedInForAccountStorage() const {
  return false;
}

bool WebViewPasswordFeatureManager::ShouldShowAccountStorageOptIn() const {
  return false;
}

void WebViewPasswordFeatureManager::SetAccountStorageOptIn(bool opt_in) {
  NOTREACHED();
}

bool WebViewPasswordFeatureManager::ShouldShowPasswordStorePicker() const {
  return false;
}

autofill::PasswordForm::Store
WebViewPasswordFeatureManager::GetDefaultPasswordStore() const {
  return autofill::PasswordForm::Store::kProfileStore;
}

void WebViewPasswordFeatureManager::SetDefaultPasswordStore(
    const autofill::PasswordForm::Store& store) {
  NOTREACHED();
}

}  // namespace ios_web_view
