// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/components/multidevice/remote_device_cache.h"

#include "base/memory/ptr_util.h"
#include "base/no_destructor.h"
#include "base/stl_util.h"
#include "chromeos/components/multidevice/logging/logging.h"

namespace chromeos {

namespace multidevice {

// static
RemoteDeviceCache::Factory* RemoteDeviceCache::Factory::test_factory_ = nullptr;

// static
RemoteDeviceCache::Factory* RemoteDeviceCache::Factory::Get() {
  if (test_factory_)
    return test_factory_;

  static base::NoDestructor<Factory> factory;
  return factory.get();
}

// static
void RemoteDeviceCache::Factory::SetFactoryForTesting(Factory* test_factory) {
  test_factory_ = test_factory;
}

RemoteDeviceCache::Factory::~Factory() = default;

std::unique_ptr<RemoteDeviceCache> RemoteDeviceCache::Factory::BuildInstance() {
  return base::WrapUnique(new RemoteDeviceCache());
}

RemoteDeviceCache::RemoteDeviceCache() = default;

RemoteDeviceCache::~RemoteDeviceCache() = default;

void RemoteDeviceCache::SetRemoteDevices(
    const RemoteDeviceList& remote_devices) {
  for (const RemoteDevice& remote_device : remote_devices) {
    if (remote_device.instance_id.empty() &&
        remote_device.GetDeviceId().empty()) {
      PA_LOG(ERROR) << "Cannot add RemoteDevice with missing Instance ID and "
                    << "legacy device ID to cache.";
      continue;
    }

    std::shared_ptr<RemoteDevice> cached_device = GetRemoteDeviceFromCache(
        remote_device.instance_id, remote_device.GetDeviceId());
    if (cached_device) {
      // Keep the same shared remote device pointer, and simply update the
      // RemoteDevice it references. This transparently updates the
      // RemoteDeviceRefs used by clients.
      // TODO(https://crbug.com/856746): Do not overwrite device metadata if
      // the new device contains a stale timestamp.
      *cached_device = remote_device;
    } else {
      cached_remote_devices_.push_back(
          std::make_shared<RemoteDevice>(remote_device));
    }
  }

  // Intentionally leave behind devices in the map which weren't in
  // |remote_devices|, to prevent clients from segfaulting by accessing
  // "stale" devices.
}

RemoteDeviceRefList RemoteDeviceCache::GetRemoteDevices() const {
  RemoteDeviceRefList remote_devices;
  for (const std::shared_ptr<RemoteDevice>& device : cached_remote_devices_) {
    remote_devices.push_back(RemoteDeviceRef(device));
  }

  return remote_devices;
}

base::Optional<RemoteDeviceRef> RemoteDeviceCache::GetRemoteDevice(
    const base::Optional<std::string>& instance_id,
    const base::Optional<std::string>& legacy_device_id) const {
  DCHECK((instance_id && !instance_id->empty()) ||
         (legacy_device_id && !legacy_device_id->empty()));

  std::shared_ptr<RemoteDevice> cached_device =
      GetRemoteDeviceFromCache(instance_id, legacy_device_id);
  if (cached_device)
    return RemoteDeviceRef(cached_device);

  return base::nullopt;
}

std::shared_ptr<RemoteDevice> RemoteDeviceCache::GetRemoteDeviceFromCache(
    const base::Optional<std::string>& instance_id,
    const base::Optional<std::string>& legacy_device_id) const {
  for (const std::shared_ptr<RemoteDevice>& cached_device :
       cached_remote_devices_) {
    if (instance_id && !instance_id->empty() &&
        cached_device->instance_id == *instance_id) {
      return cached_device;
    }

    if (legacy_device_id && !legacy_device_id->empty() &&
        cached_device->GetDeviceId() == *legacy_device_id) {
      return cached_device;
    }
  }

  return nullptr;
}

}  // namespace multidevice

}  // namespace chromeos
