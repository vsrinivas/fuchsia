// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/virtualization/testing/fake_manager.h>
#include <lib/virtualization/testing/guest_cid.h>

#include "src/lib/fxl/logging.h"

namespace guest {
namespace testing {

void FakeManager::Create(
    fidl::StringPtr label,
    fidl::InterfaceRequest<fuchsia::virtualization::Realm> request) {
  FXL_CHECK(!realm_binding_.is_bound()) << "Realm is already bound";
  realm_binding_.Bind(std::move(request));
}

void FakeManager::LaunchInstance(
    fuchsia::virtualization::LaunchInfo launch_info,
    fidl::InterfaceRequest<fuchsia::virtualization::Guest> request,
    LaunchInstanceCallback callback) {
  FXL_CHECK(!guest_binding_.is_bound()) << "Guest is already bound";
  guest_binding_.Bind(std::move(request));
  callback(kGuestCid);
}

void FakeManager::GetHostVsockEndpoint(
    fidl::InterfaceRequest<fuchsia::virtualization::HostVsockEndpoint>
        request) {
  host_vsock_.AddBinding(std::move(request));
}

// Methods below are not supported by the FakeManager:

void FakeManager::GetSerial(GetSerialCallback callback) {
  FXL_CHECK(false) << "Guest::GetSerial is not supported by "
                      "FakeManager";
  callback(zx::socket());
}

void FakeManager::List(ListCallback callback) {
  FXL_CHECK(false) << "Manager::List is not supported by "
                      "FakeManager";
  callback({});
}

void FakeManager::Connect(
    uint32_t id, fidl::InterfaceRequest<fuchsia::virtualization::Realm> env) {
  FXL_CHECK(false) << "Manager::Connect is not supported by "
                      "FakeManager";
}

void FakeManager::ListInstances(ListInstancesCallback callback) {
  FXL_CHECK(false) << "Realm::List is not supported by "
                      "FakeManager";
  callback({});
}

void FakeManager::ConnectToInstance(
    uint32_t id,
    fidl::InterfaceRequest<fuchsia::virtualization::Guest> controller) {
  FXL_CHECK(false) << "Realm::ConnectToInstance is not "
                      "supported by FakeManager";
}

void FakeManager::ConnectToBalloon(
    uint32_t id,
    fidl::InterfaceRequest<fuchsia::virtualization::BalloonController>
        controller) {
  FXL_CHECK(false) << "Realm::ConnectToBalloon is not "
                      "supported by FakeManager";
}

}  // namespace testing
}  // namespace guest
