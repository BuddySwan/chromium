// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/multidevice_setup/host_backend_delegate_impl.h"

#include <algorithm>
#include <sstream>

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "base/no_destructor.h"
#include "base/stl_util.h"
#include "chromeos/components/multidevice/logging/logging.h"
#include "chromeos/components/multidevice/software_feature.h"
#include "chromeos/components/multidevice/software_feature_state.h"
#include "chromeos/constants/chromeos_features.h"
#include "chromeos/services/device_sync/feature_status_change.h"
#include "chromeos/services/multidevice_setup/eligible_host_devices_provider.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace chromeos {

namespace multidevice_setup {

namespace {

// Name of the pref which stores the ID of the host which is pending being set
// on the back-end.
const char kPendingRequestHostIdPrefName[] =
    "multidevice_setup.pending_request_host_id";

// String to use for the pending host ID preference entry when the pending
// request is to remove the current host.
const char kPendingRemovalOfCurrentHost[] = "pendingRemovalOfCurrentHost";

// String to use for the pending host ID when there is no pending request.
const char kNoPendingRequest[] = "";

// The number of minutes to wait before retrying a failed attempt.
const int kNumMinutesBetweenRetries = 5;

const char kNoHostForLogging[] = "[no host]";

std::string GenerateDeviceIdString(multidevice::RemoteDeviceRef device) {
  std::stringstream ss;
  ss << "  Instance ID: "
     << (device.instance_id().empty() ? "[empty]" : device.instance_id())
     << "\n  Device ID: "
     << (device.GetTruncatedDeviceIdForLogs().empty()
             ? "[empty]"
             : device.GetTruncatedDeviceIdForLogs());
  return ss.str();
}

}  // namespace

// static
HostBackendDelegateImpl::Factory*
    HostBackendDelegateImpl::Factory::test_factory_ = nullptr;

// static
HostBackendDelegateImpl::Factory* HostBackendDelegateImpl::Factory::Get() {
  if (test_factory_)
    return test_factory_;

  static base::NoDestructor<Factory> factory;
  return factory.get();
}

// static
void HostBackendDelegateImpl::Factory::SetFactoryForTesting(
    Factory* test_factory) {
  test_factory_ = test_factory;
}

HostBackendDelegateImpl::Factory::~Factory() = default;

std::unique_ptr<HostBackendDelegate>
HostBackendDelegateImpl::Factory::BuildInstance(
    EligibleHostDevicesProvider* eligible_host_devices_provider,
    PrefService* pref_service,
    device_sync::DeviceSyncClient* device_sync_client,
    std::unique_ptr<base::OneShotTimer> timer) {
  return base::WrapUnique(
      new HostBackendDelegateImpl(eligible_host_devices_provider, pref_service,
                                  device_sync_client, std::move(timer)));
}

// static
void HostBackendDelegateImpl::RegisterPrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(kPendingRequestHostIdPrefName,
                               kNoPendingRequest);
}

HostBackendDelegateImpl::HostBackendDelegateImpl(
    EligibleHostDevicesProvider* eligible_host_devices_provider,
    PrefService* pref_service,
    device_sync::DeviceSyncClient* device_sync_client,
    std::unique_ptr<base::OneShotTimer> timer)
    : HostBackendDelegate(),
      eligible_host_devices_provider_(eligible_host_devices_provider),
      pref_service_(pref_service),
      device_sync_client_(device_sync_client),
      timer_(std::move(timer)) {
  device_sync_client_->AddObserver(this);

  host_from_last_sync_ = GetHostFromDeviceSync();

  if (HasPendingHostRequest())
    AttemptNetworkRequest(false /* is_retry */);
}

HostBackendDelegateImpl::~HostBackendDelegateImpl() {
  device_sync_client_->RemoveObserver(this);
}

void HostBackendDelegateImpl::AttemptToSetMultiDeviceHostOnBackend(
    const base::Optional<multidevice::RemoteDeviceRef>& host_device) {
  if (host_device && !IsHostEligible(*host_device)) {
    PA_LOG(WARNING) << "HostBackendDelegateImpl::"
                    << "AttemptToSetMultiDeviceHostOnBackend(): Tried to set a "
                    << "device as host, but that device is not an eligible "
                    << "host.\n"
                    << GenerateDeviceIdString(*host_device);
    return;
  }

  // If the device on the back-end is already |host_device|, there no longer
  // needs to be a pending request.
  if (host_from_last_sync_ == host_device) {
    SetPendingHostRequest(kNoPendingRequest);
    return;
  }

  // Stop the timer, since a new attempt is being started.
  timer_->Stop();

  if (host_device) {
    // If an Instance ID is available, use that to identify the device;
    // otherwise, use the encoded public key.
    if (features::ShouldUseV1DeviceSync()) {
      SetPendingHostRequest(host_device->instance_id().empty()
                                ? host_device->GetDeviceId()
                                : host_device->instance_id());
    } else {
      DCHECK(!host_device->instance_id().empty());
      SetPendingHostRequest(host_device->instance_id());
    }
  } else {
    SetPendingHostRequest(kPendingRemovalOfCurrentHost);
  }

  AttemptNetworkRequest(false /* is_retry */);
}

bool HostBackendDelegateImpl::HasPendingHostRequest() {
  const std::string pending_host_id_from_prefs =
      pref_service_->GetString(kPendingRequestHostIdPrefName);

  if (pending_host_id_from_prefs == kNoPendingRequest)
    return false;

  if (pending_host_id_from_prefs == kPendingRemovalOfCurrentHost) {
    // If the pending request is to remove the current host but there is no
    // current host, the host was removed by another device while this device
    // was offline.
    if (!host_from_last_sync_) {
      SetPendingHostRequest(kNoPendingRequest);
      return false;
    }

    // Otherwise, there still is a pending request to remove the current host.
    return true;
  }

  // By this point, |pending_host_id_from_prefs| refers to a real device ID and
  // not one of the two sentinel values.
  if (FindDeviceById(pending_host_id_from_prefs))
    return true;

  // If a request was pending for a specific host device, but that device is no
  // longer present on the user's account, there is no longer a pending request.
  // TODO(https://crbug.com/936273): Track frequency of unrecognized host IDs.
  // If the following scenarios occur before the pending host request completes,
  // the persisted host ID will not be recognized, and the user will need to go
  // through setup again:
  //  * The device was actually removed from the user's account.
  //  * Instance ID is persisted and v2 DeviceSync is rolled back.
  //  * A public key is persisted, v1 DeviceSync is disabled, and the v2 device
  //    data hasn't been decrypted.
  //  * v1 and v2 DeviceSync are running in parallel, an Instance ID is
  //    persisted, the device metadata is encrypted with a new group key,
  //    resulting in v1 device data being used.
  // We expect all of these scenarios to be very rare.
  SetPendingHostRequest(kNoPendingRequest);
  return false;
}

base::Optional<multidevice::RemoteDeviceRef>
HostBackendDelegateImpl::GetPendingHostRequest() const {
  const std::string pending_host_id_from_prefs =
      pref_service_->GetString(kPendingRequestHostIdPrefName);

  DCHECK_NE(pending_host_id_from_prefs, kNoPendingRequest)
      << "HostBackendDelegateImpl::GetPendingHostRequest(): Tried "
      << "to get pending host request, but there was no pending "
      << "host request.";

  if (pending_host_id_from_prefs == kPendingRemovalOfCurrentHost)
    return base::nullopt;

  base::Optional<multidevice::RemoteDeviceRef> pending_host =
      FindDeviceById(pending_host_id_from_prefs);
  DCHECK(pending_host)
      << "HostBackendDelegateImpl::GetPendingHostRequest(): Tried to get "
      << "pending host request, but the pending host ID was not present.";

  return pending_host;
}

base::Optional<multidevice::RemoteDeviceRef>
HostBackendDelegateImpl::GetMultiDeviceHostFromBackend() const {
  return host_from_last_sync_;
}

bool HostBackendDelegateImpl::IsHostEligible(
    const multidevice::RemoteDeviceRef& provided_host) {
  return base::Contains(
      eligible_host_devices_provider_->GetEligibleHostDevices(), provided_host);
}

void HostBackendDelegateImpl::SetPendingHostRequest(
    const std::string& pending_host_id) {
  const std::string host_id_from_prefs_before_call =
      pref_service_->GetString(kPendingRequestHostIdPrefName);
  if (pending_host_id == host_id_from_prefs_before_call)
    return;

  pref_service_->SetString(kPendingRequestHostIdPrefName, pending_host_id);
  timer_->Stop();
  NotifyPendingHostRequestChange();
}

base::Optional<multidevice::RemoteDeviceRef>
HostBackendDelegateImpl::FindDeviceById(const std::string& id) const {
  DCHECK(!id.empty());
  for (const auto& remote_device : device_sync_client_->GetSyncedDevices()) {
    if (features::ShouldUseV1DeviceSync()) {
      if (id == remote_device.instance_id() ||
          id == remote_device.GetDeviceId())
        return remote_device;
    } else {
      if (id == remote_device.instance_id())
        return remote_device;
    }
  }

  return base::nullopt;
}

void HostBackendDelegateImpl::AttemptNetworkRequest(bool is_retry) {
  DCHECK(HasPendingHostRequest())
      << "HostBackendDelegateImpl::AttemptNetworkRequest(): Tried to attempt a "
      << "network request, but there was no pending host request.";

  base::Optional<multidevice::RemoteDeviceRef> pending_host_request =
      GetPendingHostRequest();

  // If |pending_host_request| is non-null, the request should be to set that
  // device. If it is null, the pending request is to remove the current host.
  multidevice::RemoteDeviceRef device_to_set =
      pending_host_request ? *pending_host_request : *host_from_last_sync_;

  // Likewise, if |pending_host_request| is non-null, that device should be
  // enabled, and if it is null, the old device should be disabled.
  bool should_enable = pending_host_request != base::nullopt;

  PA_LOG(INFO) << "HostBackendDelegateImpl::AttemptNetworkRequest(): "
               << (is_retry ? "Retrying attempt" : "Attempting") << " to "
               << (should_enable ? "enable" : "disable") << " the host.\n"
               << GenerateDeviceIdString(device_to_set);

  if (features::ShouldUseV1DeviceSync()) {
    // In order to avoid a race condition in mutating the BetterTogether host
    // state on the CryptAuth backend, we are assuming that all
    // SetFeatureStatus() and SetSoftwareFeatureState() requests are added to
    // the same queue and processed in order. The DeviceSync service
    // implementation guarantees this ordering.
    if (!device_to_set.instance_id().empty()) {
      device_sync_client_->SetFeatureStatus(
          device_to_set.instance_id(),
          multidevice::SoftwareFeature::kBetterTogetherHost,
          should_enable ? device_sync::FeatureStatusChange::kEnableExclusively
                        : device_sync::FeatureStatusChange::kDisable,
          base::BindOnce(
              &HostBackendDelegateImpl::OnSetHostNetworkRequestFinished,
              weak_ptr_factory_.GetWeakPtr(), device_to_set, should_enable));

    } else {
      DCHECK(!device_to_set.public_key().empty());
      device_sync_client_->SetSoftwareFeatureState(
          device_to_set.public_key(),
          multidevice::SoftwareFeature::kBetterTogetherHost,
          should_enable /* enabled */, should_enable /* is_exclusive */,
          base::BindOnce(
              &HostBackendDelegateImpl::OnSetHostNetworkRequestFinished,
              weak_ptr_factory_.GetWeakPtr(), device_to_set, should_enable));
    }
  } else {
    DCHECK(!device_to_set.instance_id().empty());
    device_sync_client_->SetFeatureStatus(
        device_to_set.instance_id(),
        multidevice::SoftwareFeature::kBetterTogetherHost,
        should_enable ? device_sync::FeatureStatusChange::kEnableExclusively
                      : device_sync::FeatureStatusChange::kDisable,
        base::BindOnce(
            &HostBackendDelegateImpl::OnSetHostNetworkRequestFinished,
            weak_ptr_factory_.GetWeakPtr(), device_to_set, should_enable));
  }
}

void HostBackendDelegateImpl::OnNewDevicesSynced() {
  base::Optional<multidevice::RemoteDeviceRef> host_from_sync =
      GetHostFromDeviceSync();
  if (host_from_last_sync_ == host_from_sync)
    return;

  std::string old_host_ids =
      host_from_last_sync_
          ? ('\n' + GenerateDeviceIdString(*host_from_last_sync_))
          : kNoHostForLogging;
  std::string new_host_ids =
      host_from_sync ? ('\n' + GenerateDeviceIdString(*host_from_sync))
                     : kNoHostForLogging;

  host_from_last_sync_ = host_from_sync;
  PA_LOG(VERBOSE) << "HostBackendDelegateImpl::OnNewDevicesSynced(): New host "
                  << "device has been set."
                  << "\nOld host IDs: " << old_host_ids
                  << "\nNew host IDs: " << new_host_ids;

  // If there is a pending request and the new host fulfills that pending
  // request, there is no longer a pending request.
  if (HasPendingHostRequest() &&
      host_from_last_sync_ == GetPendingHostRequest()) {
    SetPendingHostRequest(kNoPendingRequest);
  }

  NotifyHostChangedOnBackend();
}

base::Optional<multidevice::RemoteDeviceRef>
HostBackendDelegateImpl::GetHostFromDeviceSync() {
  multidevice::RemoteDeviceRefList synced_devices =
      device_sync_client_->GetSyncedDevices();
  auto it = std::find_if(
      synced_devices.begin(), synced_devices.end(),
      [](const auto& remote_device) {
        multidevice::SoftwareFeatureState host_state =
            remote_device.GetSoftwareFeatureState(
                multidevice::SoftwareFeature::kBetterTogetherHost);
        return host_state == multidevice::SoftwareFeatureState::kEnabled;
      });

  if (it == synced_devices.end())
    return base::nullopt;

  return *it;
}

void HostBackendDelegateImpl::OnSetHostNetworkRequestFinished(
    multidevice::RemoteDeviceRef device_for_request,
    bool attempted_to_enable,
    device_sync::mojom::NetworkRequestResult result_code) {
  bool success =
      result_code == device_sync::mojom::NetworkRequestResult::kSuccess;

  std::stringstream ss;
  ss << "HostBackendDelegateImpl::OnSetHostNetworkRequestFinished(): "
     << (success ? "Completed successful" : "Failure requesting") << " "
     << "host change.\n"
     << GenerateDeviceIdString(device_for_request)
     << "\nAttempted to enable: " << (attempted_to_enable ? "true" : "false");

  if (success) {
    PA_LOG(VERBOSE) << ss.str();
    return;
  }

  ss << ", Error code: " << result_code;
  PA_LOG(WARNING) << ss.str();

  if (!HasPendingHostRequest())
    return;

  base::Optional<multidevice::RemoteDeviceRef> pending_host_request =
      GetPendingHostRequest();

  bool failed_request_was_to_set_pending_host =
      attempted_to_enable && pending_host_request &&
      *pending_host_request == device_for_request;

  bool failed_request_was_to_remove_pending_host =
      !attempted_to_enable && !pending_host_request &&
      device_for_request == host_from_last_sync_;

  // If the request which failed corresponds to the most recent call to
  // AttemptToSetMultiDeviceHostOnBackend(), alert observers that this request
  // failed and schedule a retry.
  if (failed_request_was_to_set_pending_host ||
      failed_request_was_to_remove_pending_host) {
    NotifyBackendRequestFailed();
    timer_->Start(FROM_HERE,
                  base::TimeDelta::FromMinutes(kNumMinutesBetweenRetries),
                  base::Bind(&HostBackendDelegateImpl::AttemptNetworkRequest,
                             base::Unretained(this), true /* is_retry */));
  }
}

}  // namespace multidevice_setup

}  // namespace chromeos
