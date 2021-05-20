// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/internal/DeviceNetworkInfo.h>
#include <Weave/DeviceLayer/internal/NetworkProvisioningServer.h>
#include <Weave/Core/WeaveTLV.h>
#include <Weave/Profiles/WeaveProfiles.h>
#include <Weave/Profiles/common/CommonProfile.h>

#include <Weave/DeviceLayer/internal/GenericNetworkProvisioningServerImpl.ipp>
// clang-format on

#include <lib/syslog/cpp/macros.h>

namespace nl::Weave::DeviceLayer::Internal {

NetworkProvisioningServerImpl NetworkProvisioningServerImpl::sInstance;

void NetworkProvisioningServerImpl::SetDelegate(std::unique_ptr<Delegate> delegate) {
  FX_CHECK(!(delegate && delegate_)) << "Attempted to set an already set delegate. Must explicitly "
                                        "clear the existing delegate first.";
  delegate_ = std::move(delegate);
  if (delegate_) {
    delegate_->SetNetworkProvisioningServerImpl(this);
  }
}

WEAVE_ERROR NetworkProvisioningServerImpl::_Init() {
  WEAVE_ERROR err;

  err = delegate_->Init();
  if (err != WEAVE_NO_ERROR) {
    return err;
  }
  return GenericNetworkProvisioningServerImpl<NetworkProvisioningServerImpl>::DoInit();
}

void NetworkProvisioningServerImpl::SetWlanNetworkConfigProvider(
    ::fidl::InterfaceHandle<class ::fuchsia::weave::WlanNetworkConfigProvider> provider) {
  delegate_->SetWlanNetworkConfigProvider(std::move(provider));
}

void NetworkProvisioningServerImpl::_OnPlatformEvent(const WeaveDeviceEvent* event) {
  // Propagate the event to the GenericNetworkProvisioningServerImpl<> base class so
  // that it can take action on specific events.
  GenericImplClass::_OnPlatformEvent(event);
}

WEAVE_ERROR NetworkProvisioningServerImpl::GetWiFiStationProvision(NetworkInfo& net_info,
                                                                   bool include_credentials) {
  return delegate_->GetWiFiStationProvision(net_info, include_credentials);
}

}  // namespace nl::Weave::DeviceLayer::Internal
