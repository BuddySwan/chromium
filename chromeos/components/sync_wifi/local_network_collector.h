// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_COMPONENTS_SYNC_WIFI_LOCAL_NETWORK_COLLECTOR_H_
#define CHROMEOS_COMPONENTS_SYNC_WIFI_LOCAL_NETWORK_COLLECTOR_H_

#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/optional.h"
#include "chromeos/services/network_config/public/mojom/cros_network_config.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"

namespace sync_pb {
class WifiConfigurationSpecifics;
}

namespace chromeos {

namespace sync_wifi {

class NetworkIdentifier;

// Handles the retrieval, filtering, and conversion of local network
// configurations to syncable protos.
class LocalNetworkCollector {
 public:
  typedef base::OnceCallback<void(
      base::Optional<sync_pb::WifiConfigurationSpecifics>)>
      ProtoCallback;

  typedef base::OnceCallback<void(
      std::vector<sync_pb::WifiConfigurationSpecifics>)>
      ProtoListCallback;

  LocalNetworkCollector() = default;
  virtual ~LocalNetworkCollector() = default;

  // Creates a list of all local networks which are syncable and delivers them
  // to the provided |callback|.  This excludes networks which are managed,
  // unsecured, use enterprise security, or shared.
  virtual void GetAllSyncableNetworks(ProtoListCallback callback) = 0;

  // Creates a WifiConfigurationSpecifics proto with the relevant network
  // details for the network with the given |id|.  If that network doesn't
  // exist or isn't syncable it will provide base::nullopt to the callback.
  virtual void GetSyncableNetwork(const NetworkIdentifier& id,
                                  ProtoCallback callback) = 0;
};

}  // namespace sync_wifi

}  // namespace chromeos

#endif  // CHROMEOS_COMPONENTS_SYNC_WIFI_LOCAL_NETWORK_COLLECTOR_H_
