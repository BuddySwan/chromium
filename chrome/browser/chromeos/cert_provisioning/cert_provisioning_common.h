// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_CERT_PROVISIONING_CERT_PROVISIONING_COMMON_H_
#define CHROME_BROWSER_CHROMEOS_CERT_PROVISIONING_CERT_PROVISIONING_COMMON_H_

class PrefRegistrySimple;

namespace chromeos {
namespace cert_provisioning {

void RegisterProfilePrefs(PrefRegistrySimple* registry);
void RegisterLocalStatePrefs(PrefRegistrySimple* registry);

}  // namespace cert_provisioning
}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_CERT_PROVISIONING_CERT_PROVISIONING_COMMON_H_
