// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/guest/testing/fake_environment_manager.h>
#include <lib/guest/testing/guest_cid.h>

#include "src/lib/fxl/logging.h"

namespace guest {
namespace testing {

void FakeEnvironmentManager::Create(
    fidl::StringPtr label,
    fidl::InterfaceRequest<fuchsia::guest::EnvironmentController> request) {
  FXL_CHECK(!environment_controller_binding_.is_bound())
      << "EnvironmentController is already bound";
  environment_controller_binding_.Bind(std::move(request));
}

void FakeEnvironmentManager::LaunchInstance(
    fuchsia::guest::LaunchInfo launch_info,
    fidl::InterfaceRequest<fuchsia::guest::InstanceController> request,
    LaunchInstanceCallback callback) {
  FXL_CHECK(!instance_controller_binding_.is_bound())
      << "InstanceController is already bound";
  instance_controller_binding_.Bind(std::move(request));
  callback(kGuestCid);
}

void FakeEnvironmentManager::GetHostVsockEndpoint(
    fidl::InterfaceRequest<fuchsia::guest::HostVsockEndpoint> request) {
  host_vsock_.AddBinding(std::move(request));
}

// Methods below are not supported by the FakeEnvironmentManager:

void FakeEnvironmentManager::GetSerial(GetSerialCallback callback) {
  FXL_CHECK(false) << "InstanceController::GetSerial is not supported by "
                      "FakeEnvironmentManager";
  callback(zx::socket());
}

void FakeEnvironmentManager::List(ListCallback callback) {
  FXL_CHECK(false) << "EnvironmentManager::List is not supported by "
                      "FakeEnvironmentManager";
  callback({});
}

void FakeEnvironmentManager::Connect(
    uint32_t id,
    fidl::InterfaceRequest<fuchsia::guest::EnvironmentController> env) {
  FXL_CHECK(false) << "EnvironmentManager::Connect is not supported by "
                      "FakeEnvironmentManager";
}

void FakeEnvironmentManager::ListInstances(ListInstancesCallback callback) {
  FXL_CHECK(false) << "EnvironmentController::List is not supported by "
                      "FakeEnvironmentManager";
  callback({});
}

void FakeEnvironmentManager::ConnectToInstance(
    uint32_t id,
    fidl::InterfaceRequest<fuchsia::guest::InstanceController> controller) {
  FXL_CHECK(false) << "EnvironmentController::ConnectToInstance is not "
                      "supported by FakeEnvironmentManager";
}

void FakeEnvironmentManager::ConnectToBalloon(
    uint32_t id,
    fidl::InterfaceRequest<fuchsia::guest::BalloonController> controller) {
  FXL_CHECK(false) << "EnvironmentController::ConnectToBalloon is not "
                      "supported by FakeEnvironmentManager";
}

}  // namespace testing
}  // namespace guest
