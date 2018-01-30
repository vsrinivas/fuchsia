// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/netconnector/fake_netconnector.h"
#include "lib/fxl/functional/make_copyable.h"

namespace ledger {
FakeNetConnector::FakeNetConnector(Delegate* delegate) : delegate_(delegate) {}

void FakeNetConnector::ConnectToServiceProvider(
    fidl::InterfaceRequest<app::ServiceProvider> request) {
  service_provider_impl_.AddBinding(std::move(request));
}

void FakeNetConnector::RegisterServiceProvider(
    const fidl::String& name,
    fidl::InterfaceHandle<app::ServiceProvider> service_provider) {
  app::ServiceProviderPtr service_provider_ptr = service_provider.Bind();
  service_provider_impl_.AddServiceForName(
      fxl::MakeCopyable([name, service_provider_ptr = std::move(
                                   service_provider_ptr)](zx::channel channel) {
        service_provider_ptr->ConnectToService(name, std::move(channel));
      }),
      name);
}

void FakeNetConnector::GetDeviceServiceProvider(
    const fidl::String& device_name,
    fidl::InterfaceRequest<app::ServiceProvider> service_provider) {
  delegate_->ConnectToServiceProvider(device_name, std::move(service_provider));
}

void FakeNetConnector::GetKnownDeviceNames(
    uint64_t version_last_seen,
    const GetKnownDeviceNamesCallback& callback) {
  delegate_->GetDevicesNames(version_last_seen, callback);
}

}  // namespace ledger
