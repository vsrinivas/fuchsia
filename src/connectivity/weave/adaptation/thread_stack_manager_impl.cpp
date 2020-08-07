// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/PlatformManager.h>
#include <Weave/DeviceLayer/ThreadStackManager.h>
// clang-format on

namespace nl {
namespace Weave {
namespace DeviceLayer {

namespace {
using fuchsia::lowpan::ConnectivityState;
using fuchsia::lowpan::device::DeviceState;
using fuchsia::lowpan::device::DeviceSyncPtr;
using fuchsia::lowpan::device::Lookup_LookupDevice_Result;
using fuchsia::lowpan::device::LookupSyncPtr;
using fuchsia::lowpan::device::Protocols;
using nl::Weave::DeviceLayer::PlatformMgrImpl;
using ThreadDeviceType = ConnectivityManager::ThreadDeviceType;
}  // namespace

// Note: Since the functions within this class are intended to function
// synchronously within the Device Layer, these functions all use SyncPtrs for
// interfacing with the LoWPAN FIDL protocols.

ThreadStackManagerImpl ThreadStackManagerImpl::sInstance;

WEAVE_ERROR ThreadStackManagerImpl::_InitThreadStack() {
  // See note at top to explain these SyncPtrs
  LookupSyncPtr lookup;
  Lookup_LookupDevice_Result result;
  Protocols protocols;
  std::vector<std::string> interface_names;
  zx_status_t status;

  // Access the LoWPAN service
  status = PlatformMgrImpl().GetComponentContextForProcess()->svc()->Connect(lookup.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to connect to fuchsia.lowpan.device.Lookup: "
                   << zx_status_get_string(status);
    return status;
  }

  // Retrieve LoWPAN interface names
  status = lookup->GetDevices(&interface_names);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to retrieve LoWPAN interface names: " << zx_status_get_string(status);
    return status;
  }

  // Check returned interfaces for Thread support
  bool found_device = false;
  for (auto& name : interface_names) {
    std::vector<std::string> net_types;

    protocols.set_device(device_.NewRequest());

    // Look up the device by interface name
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

    // Check if the device supports Thread
    status = device_->GetSupportedNetworkTypes(&net_types);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to request supported network types from device \"" << name
                     << "\": " << zx_status_get_string(status);
      return status;
    }

    for (auto& net_type : net_types) {
      if (net_type == fuchsia::lowpan::NET_TYPE_THREAD_1_X) {
        // Found a Thread device
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

  // Get the device state
  status = GetDeviceState(&device_state);
  if (status != ZX_OK)
    return false;

  // Determine whether Thread is enabled
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
  // Enable or disable the device
  zx_status_t status = device_->SetActive(val);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to " << (val ? "enable" : "disable")
                   << " Thread: " << zx_status_get_string(status);
    return status;
  }

  return WEAVE_NO_ERROR;
}

bool ThreadStackManagerImpl::_IsThreadProvisioned() {
  return false;  // TODO(fxbug.dev/55854)
}

bool ThreadStackManagerImpl::_IsThreadAttached() {
  DeviceState device_state;
  zx_status_t status;

  // Get the device state
  status = GetDeviceState(&device_state);
  if (status != ZX_OK)
    return false;

  return device_state.connectivity_state() == ConnectivityState::ATTACHED;
}

WEAVE_ERROR ThreadStackManagerImpl::_GetThreadProvision(Internal::DeviceNetworkInfo& netInfo,
                                                        bool includeCredentials) {
  return WEAVE_ERROR_NOT_IMPLEMENTED;  // TODO(fxbug.dev/55854)
}

WEAVE_ERROR ThreadStackManagerImpl::_SetThreadProvision(
    const Internal::DeviceNetworkInfo& netInfo) {
  return WEAVE_ERROR_NOT_IMPLEMENTED;  // TODO(fxbug.dev/55854)
}

void ThreadStackManagerImpl::_ClearThreadProvision() {
  // TODO
}

ThreadDeviceType ThreadStackManagerImpl::_GetThreadDeviceType() {
  return ThreadDeviceType::kThreadDeviceType_NotSupported;  // TODO(fxbug.dev/55855)
}

bool ThreadStackManagerImpl::_HaveMeshConnectivity() {
  return false;  // TODO(fxbug.dev/55855)
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

  // Get device pointer
  status = GetProtocols(std::move(Protocols().set_device(device.NewRequest())));
  if (status != ZX_OK) {
    return status;
  }

  // Grab device state
  status = device->WatchDeviceState(device_state);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not get LoWPAN device state: " << zx_status_get_string(status);
    return status;
  }

  return ZX_OK;
}

zx_status_t ThreadStackManagerImpl::GetProtocols(Protocols protocols) {
  // See note at top to explain these SyncPtrs
  LookupSyncPtr lookup;
  Lookup_LookupDevice_Result result;
  zx_status_t status;

  // Access the LoWPAN service
  status = PlatformMgrImpl().GetComponentContextForProcess()->svc()->Connect(lookup.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to connect to fuchsia.lowpan.device.Lookup: "
                   << zx_status_get_string(status);
    return status;
  }

  // Look up the device by interface name
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
