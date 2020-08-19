// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/internal/DeviceNetworkInfo.h>
#include <Weave/DeviceLayer/PlatformManager.h>
#include <Weave/DeviceLayer/ThreadStackManager.h>
// clang-format on

namespace nl {
namespace Weave {
namespace DeviceLayer {

namespace {
using fuchsia::lowpan::ConnectivityState;
using fuchsia::lowpan::Credential;
using fuchsia::lowpan::Identity;
using fuchsia::lowpan::ProvisioningParams;
using fuchsia::lowpan::Role;
using fuchsia::lowpan::device::DeviceExtraSyncPtr;
using fuchsia::lowpan::device::DeviceState;
using fuchsia::lowpan::device::DeviceSyncPtr;
using fuchsia::lowpan::device::Lookup_LookupDevice_Result;
using fuchsia::lowpan::device::LookupSyncPtr;
using fuchsia::lowpan::device::Protocols;

using nl::Weave::DeviceLayer::PlatformMgrImpl;
using nl::Weave::DeviceLayer::Internal::DeviceNetworkInfo;

using ThreadDeviceType = ConnectivityManager::ThreadDeviceType;

constexpr uint16_t kMinThreadChannel = 11;
constexpr uint16_t kMaxThreadChannel = 26;
}  // namespace

// Note: Since the functions within this class are intended to function
// synchronously within the Device Layer, these functions all use SyncPtrs for
// interfacing with the LoWPAN FIDL protocols.

ThreadStackManagerImpl ThreadStackManagerImpl::sInstance;

WEAVE_ERROR ThreadStackManagerImpl::_InitThreadStack() {
  // See note at top to explain these SyncPtrs.
  LookupSyncPtr lookup;
  Lookup_LookupDevice_Result result;
  Protocols protocols;
  std::vector<std::string> interface_names;
  zx_status_t status;

  // Access the LoWPAN service.
  status = PlatformMgrImpl().GetComponentContextForProcess()->svc()->Connect(lookup.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to connect to fuchsia.lowpan.device.Lookup: "
                   << zx_status_get_string(status);
    return status;
  }

  // Retrieve LoWPAN interface names.
  status = lookup->GetDevices(&interface_names);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to retrieve LoWPAN interface names: " << zx_status_get_string(status);
    return status;
  }

  // Check returned interfaces for Thread support.
  bool found_device = false;
  for (auto& name : interface_names) {
    std::vector<std::string> net_types;

    protocols.set_device(device_.NewRequest());

    // Look up the device by interface name.
    status = lookup->LookupDevice(name, std::move(protocols), &result);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to lookup device: " << zx_status_get_string(status);
      return status;
    }
    if (result.is_err()) {
      FX_LOGS(WARNING) << "LoWPAN service error during lookup: "
                       << static_cast<int32_t>(result.err());
      continue;
    }

    // Check if the device supports Thread.
    status = device_->GetSupportedNetworkTypes(&net_types);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to request supported network types from device \"" << name
                     << "\": " << zx_status_get_string(status);
      return status;
    }

    for (auto& net_type : net_types) {
      if (net_type == fuchsia::lowpan::NET_TYPE_THREAD_1_X) {
        // Found a Thread device.
        interface_name_ = name;
        found_device = true;
        break;
      }
    }

    if (found_device) {
      break;
    }
  }

  if (!found_device) {
    FX_LOGS(ERROR) << "Could not find a device that supports Thread networks!";
    return ZX_ERR_NOT_FOUND;
  }

  return WEAVE_NO_ERROR;
}

bool ThreadStackManagerImpl::_HaveRouteToAddress(const IPAddress& destAddr) {
  return false;  // TODO(fxbug.dev/55857)
}

WEAVE_ERROR ThreadStackManagerImpl::_GetPrimary802154MACAddress(uint8_t* buf) {
  return WEAVE_ERROR_UNSUPPORTED_WEAVE_FEATURE;  // TODO(fxbug.dev/55856)
}

void ThreadStackManagerImpl::_OnPlatformEvent(const WeaveDeviceEvent* event) {}

bool ThreadStackManagerImpl::_IsThreadEnabled() {
  DeviceState device_state;
  zx_status_t status;

  // Get the device state.
  status = GetDeviceState(&device_state);
  if (status != ZX_OK)
    return false;

  // Determine whether Thread is enabled.
  switch (device_state.connectivity_state()) {
    case ConnectivityState::OFFLINE:
    case ConnectivityState::ATTACHING:
    case ConnectivityState::ATTACHED:
    case ConnectivityState::ISOLATED:
    case ConnectivityState::COMMISSIONING:
      return true;
    default:
      return false;
  }
}

WEAVE_ERROR ThreadStackManagerImpl::_SetThreadEnabled(bool val) {
  // Enable or disable the device.
  zx_status_t status = device_->SetActive(val);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to " << (val ? "enable" : "disable")
                   << " Thread: " << zx_status_get_string(status);
    return status;
  }

  return WEAVE_NO_ERROR;
}

bool ThreadStackManagerImpl::_IsThreadProvisioned() {
  DeviceState device_state;
  zx_status_t status;

  // Get the device state.
  status = GetDeviceState(&device_state);
  if (status != ZX_OK)
    return false;

  // Check for the provision.
  switch (device_state.connectivity_state()) {
    case ConnectivityState::INACTIVE:
    case ConnectivityState::OFFLINE:
      return false;
    default:
      return true;
  }
}

bool ThreadStackManagerImpl::_IsThreadAttached() {
  DeviceState device_state;
  zx_status_t status;

  // Get the device state.
  status = GetDeviceState(&device_state);
  if (status != ZX_OK)
    return false;

  return device_state.connectivity_state() == ConnectivityState::ATTACHED;
}

WEAVE_ERROR ThreadStackManagerImpl::_GetThreadProvision(DeviceNetworkInfo& netInfo,
                                                        bool includeCredentials) {
  DeviceExtraSyncPtr device_extra;
  Identity identity;
  zx_status_t status;

  // Get the Device pointer.
  status = GetProtocols(std::move(Protocols().set_device_extra(device_extra.NewRequest())));
  if (status != ZX_OK)
    return status;

  // Get the network identity.
  status = device_extra->WatchIdentity(&identity);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not get LoWPAN network identity: " << zx_status_get_string(status);
    return status;
  }

  // Check if the provision is a Thread network.
  if (!identity.has_net_type()) {
    FX_LOGS(ERROR) << "No net_type provided; cannot confirm Thread network type.";
    return ZX_ERR_INTERNAL;
  }
  if (identity.net_type() != fuchsia::lowpan::NET_TYPE_THREAD_1_X) {
    FX_LOGS(ERROR) << "Cannot support LoWPAN network type \"" << identity.net_type()
                   << "\" in ThreadStackManager.";
    return ZX_ERR_INTERNAL;
  }

  // Start copying provision info.
  netInfo.Reset();
  // Copy network name.
  if (identity.has_raw_name()) {
    std::memcpy(netInfo.ThreadNetworkName, identity.raw_name().data(),
                std::min<size_t>(DeviceNetworkInfo::kMaxThreadNetworkNameLength,
                                 identity.raw_name().size()));
  }
  // Copy extended PAN id.
  if (identity.has_xpanid()) {
    std::memcpy(
        netInfo.ThreadExtendedPANId, identity.xpanid().data(),
        std::min<size_t>(DeviceNetworkInfo::kThreadExtendedPANIdLength, identity.xpanid().size()));
    netInfo.FieldPresent.ThreadExtendedPANId = true;
  }
  // Copy PAN id.
  if (!identity.has_panid()) {
    // Warn that PAN id remains unspecified.
    FX_LOGS(WARNING) << "PAN id not supplied.";
  } else {
    netInfo.ThreadPANId = identity.panid();
  }
  // Copy channel.
  if (!identity.has_channel() || identity.channel() < kMinThreadChannel ||
      identity.channel() > kMaxThreadChannel) {
    // Warn that channel remains unspecified.
    std::string channel_info =
        identity.has_channel() ? std::to_string(identity.channel()) : "(none)";
    FX_LOGS(WARNING) << "Invalid Thread channel: " << channel_info;
  } else {
    netInfo.ThreadChannel = identity.channel();
  }

  // TODO(fxbug.dev/55638) - Implement mesh prefix and pre-shared commisioning key.

  if (!includeCredentials) {
    // No futher processing needed, credentials won't be included.
    return WEAVE_NO_ERROR;
  }

  // Get credential.
  std::unique_ptr<Credential> credential;
  status = device_extra->GetCredential(&credential);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not retrieve credential: " << zx_status_get_string(status);
    return status;
  }

  // Copy credential info.
  if (!credential) {
    // Warn that credential remains unset.
    FX_LOGS(WARNING) << "Credential requested but no credential provided from LoWPAN device";
  } else {
    std::memcpy(netInfo.ThreadNetworkKey, credential->master_key().data(),
                std::min<size_t>(DeviceNetworkInfo::kMaxThreadNetworkNameLength,
                                 credential->master_key().size()));
    netInfo.FieldPresent.ThreadNetworkKey = true;
  }

  return WEAVE_NO_ERROR;
}

WEAVE_ERROR ThreadStackManagerImpl::_SetThreadProvision(const DeviceNetworkInfo& netInfo) {
  DeviceSyncPtr device;
  std::unique_ptr<Credential> credential;
  Identity identity;

  // Set up identity.
  std::vector<uint8_t> network_name{
      netInfo.ThreadNetworkName,
      netInfo.ThreadNetworkName + std::strlen(netInfo.ThreadNetworkName)};
  ;
  identity.set_raw_name(std::move(network_name));

  identity.set_net_type(fuchsia::lowpan::NET_TYPE_THREAD_1_X);

  if (netInfo.FieldPresent.ThreadExtendedPANId) {
    identity.set_xpanid(std::vector<uint8_t>{
        netInfo.ThreadExtendedPANId,
        netInfo.ThreadExtendedPANId + DeviceNetworkInfo::kThreadExtendedPANIdLength});
  }

  if (netInfo.ThreadChannel != Profiles::NetworkProvisioning::kThreadChannel_NotSpecified) {
    identity.set_channel(netInfo.ThreadChannel);
  }

  if (netInfo.ThreadPANId != Profiles::NetworkProvisioning::kThreadPANId_NotSpecified) {
    identity.set_panid(netInfo.ThreadPANId);
  }

  // Set up credential.
  if (netInfo.FieldPresent.ThreadNetworkKey) {
    credential = std::make_unique<Credential>();
    credential->set_master_key(std::vector<uint8_t>{
        netInfo.ThreadNetworkKey,
        netInfo.ThreadNetworkKey + DeviceNetworkInfo::kThreadNetworkKeyLength});
  }

  // Add identity and credential to provisioning params.
  ProvisioningParams params{.identity = std::move(identity), .credential = std::move(credential)};

  // Provision the thread device.
  return device_->ProvisionNetwork(std::move(params));
}

void ThreadStackManagerImpl::_ClearThreadProvision() {
  zx_status_t status = device_->LeaveNetwork();
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not clear LoWPAN provision: " << zx_status_get_string(status);
  }
}

ThreadDeviceType ThreadStackManagerImpl::_GetThreadDeviceType() {
  DeviceState device_state;
  zx_status_t status;

  // Get the device state.
  status = GetDeviceState(&device_state);
  if (status != ZX_OK) {
    return ThreadDeviceType::kThreadDeviceType_NotSupported;
  }

  // Determine device type by role.
  switch (device_state.role()) {
    case Role::END_DEVICE:
      return ThreadDeviceType::kThreadDeviceType_FullEndDevice;
    case Role::SLEEPY_END_DEVICE:
      return ThreadDeviceType::kThreadDeviceType_SleepyEndDevice;
    case Role::ROUTER:
    case Role::SLEEPY_ROUTER:
    case Role::LEADER:
    case Role::COORDINATOR:
      return ThreadDeviceType::kThreadDeviceType_Router;
    default:
      return ThreadDeviceType::kThreadDeviceType_NotSupported;
  };
}

bool ThreadStackManagerImpl::_HaveMeshConnectivity() {
  return _IsThreadAttached();
}

WEAVE_ERROR ThreadStackManagerImpl::_GetAndLogThreadStatsCounters() {
  return WEAVE_ERROR_NOT_IMPLEMENTED;  // TODO(fxbug.dev/55888)
}

WEAVE_ERROR ThreadStackManagerImpl::_GetAndLogThreadTopologyMinimal() {
  return WEAVE_ERROR_NOT_IMPLEMENTED;  // TODO(fxbug.dev/55888)
}

WEAVE_ERROR ThreadStackManagerImpl::_GetAndLogThreadTopologyFull() {
  return WEAVE_ERROR_NOT_IMPLEMENTED;  // TODO(fxbug.dev/55888)
}

zx_status_t ThreadStackManagerImpl::GetDeviceState(DeviceState* device_state) {
  DeviceSyncPtr device;
  zx_status_t status;

  // Get device pointer.
  status = GetProtocols(std::move(Protocols().set_device(device.NewRequest())));
  if (status != ZX_OK) {
    return status;
  }

  // Grab device state.
  status = device->WatchDeviceState(device_state);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not get LoWPAN device state: " << zx_status_get_string(status);
    return status;
  }

  return ZX_OK;
}

zx_status_t ThreadStackManagerImpl::GetProtocols(Protocols protocols) {
  // See note at top to explain these SyncPtrs.
  LookupSyncPtr lookup;
  Lookup_LookupDevice_Result result;
  zx_status_t status;

  // Access the LoWPAN service.
  status = PlatformMgrImpl().GetComponentContextForProcess()->svc()->Connect(lookup.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to connect to fuchsia.lowpan.device.Lookup: "
                   << zx_status_get_string(status);
    return status;
  }

  // Look up the device by interface name.
  status = lookup->LookupDevice(interface_name_, std::move(protocols), &result);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to lookup device: " << zx_status_get_string(status);
    return status;
  }
  if (result.is_err()) {
    FX_LOGS(ERROR) << "LoWPAN service error during lookup: " << static_cast<int32_t>(result.err());
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl
