// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/netconnector/fake_netconnector.h"

namespace ledger {
FakeNetConnector::FakeNetConnector(Delegate* delegate) : delegate_(delegate) {}

void FakeNetConnector::ConnectToServiceProvider(
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> request) {
  service_provider_impl_.AddBinding(std::move(request));
}

void FakeNetConnector::RegisterServiceProvider(
    fidl::StringPtr name,
    fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> service_provider) {
  fuchsia::sys::ServiceProviderPtr service_provider_ptr =
      service_provider.Bind();
  service_provider_impl_.AddServiceForName(
      [name, service_provider_ptr =
                 std::move(service_provider_ptr)](zx::channel channel) {
        service_provider_ptr->ConnectToService(name, std::move(channel));
      },
      name);
}

void FakeNetConnector::GetDeviceServiceProvider(
    fidl::StringPtr device_name,
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> service_provider) {
  delegate_->ConnectToServiceProvider(device_name, std::move(service_provider));
}

void FakeNetConnector::GetKnownDeviceNames(
    uint64_t version_last_seen, GetKnownDeviceNamesCallback callback) {
  delegate_->GetDevicesNames(version_last_seen, std::move(callback));
}

}  // namespace ledger
