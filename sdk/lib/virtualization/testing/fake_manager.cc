// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <lib/virtualization/testing/fake_manager.h>
#include <lib/virtualization/testing/guest_cid.h>

namespace guest {
namespace testing {

void FakeManager::Create(fidl::StringPtr label,
                         fidl::InterfaceRequest<fuchsia::virtualization::Realm> request) {
  FX_CHECK(!realm_binding_.is_bound()) << "Realm is already bound";
  realm_binding_.Bind(std::move(request));
}

void FakeManager::LaunchInstance(std::string url, fidl::StringPtr label,
                                 fuchsia::virtualization::GuestConfig guest_config,
                                 fidl::InterfaceRequest<fuchsia::virtualization::Guest> request,
                                 LaunchInstanceCallback callback) {
  FX_CHECK(!guest_binding_.is_bound()) << "Guest is already bound";
  guest_binding_.Bind(std::move(request));
  callback(kGuestCid);
}

void FakeManager::GetHostVsockEndpoint(
    fidl::InterfaceRequest<fuchsia::virtualization::HostVsockEndpoint> request) {
  host_vsock_.AddBinding(std::move(request));
}

void FakeManager::NotImplemented_(const std::string& name) {
  FX_CHECK(false) << "Method not supported by FakeManager: " << name;
}

}  // namespace testing
}  // namespace guest
