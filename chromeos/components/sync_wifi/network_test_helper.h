// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_COMPONENTS_SYNC_WIFI_NETWORK_TEST_HELPER_H_
#define CHROMEOS_COMPONENTS_SYNC_WIFI_NETWORK_TEST_HELPER_H_

#include "chromeos/network/managed_network_configuration_handler.h"
#include "chromeos/network/network_configuration_handler.h"
#include "chromeos/network/network_connection_handler.h"
#include "chromeos/network/network_device_handler.h"
#include "chromeos/network/network_profile_handler.h"
#include "chromeos/network/network_state_handler.h"
#include "chromeos/network/network_state_test_helper.h"
#include "chromeos/network/proxy/ui_proxy_config_service.h"
#include "chromeos/services/network_config/cros_network_config.h"
#include "chromeos/services/network_config/public/cpp/cros_network_config_test_helper.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "components/user_manager/scoped_user_manager.h"

namespace chromeos {

namespace sync_wifi {

// Helper for tests which need to have fake network classes configured.
class NetworkTestHelper : public network_config::CrosNetworkConfigTestHelper {
 public:
  NetworkTestHelper();
  virtual ~NetworkTestHelper();

  void SetUp();
  void ConfigureWiFiNetwork(const std::string& ssid,
                            bool is_secured,
                            bool in_profile);

 private:
  std::unique_ptr<NetworkProfileHandler> network_profile_handler_;
  std::unique_ptr<NetworkConfigurationHandler> network_configuration_handler_;
  std::unique_ptr<ManagedNetworkConfigurationHandler>
      managed_network_configuration_handler_;
  std::unique_ptr<UIProxyConfigService> ui_proxy_config_service_;
  std::unique_ptr<user_manager::ScopedUserManager> scoped_user_manager_;
  sync_preferences::TestingPrefServiceSyncable user_prefs_;
  TestingPrefServiceSimple local_state_;
};

}  // namespace sync_wifi

}  // namespace chromeos

#endif  // CHROMEOS_COMPONENTS_SYNC_WIFI_NETWORK_TEST_HELPER_H_
