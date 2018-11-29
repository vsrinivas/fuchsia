// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_INTEGRATION_TESTS_ENCLOSED_GUEST_H_
#define GARNET_BIN_GUEST_INTEGRATION_TESTS_ENCLOSED_GUEST_H_

#include <fuchsia/guest/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/environment_services_helper.h>
#include <lib/component/cpp/testing/test_with_environment.h>

#include "garnet/bin/guest/integration_tests/test_serial.h"

class EnclosedGuest {
 public:
  EnclosedGuest()
      : loop_(&kAsyncLoopConfigAttachToThread),
        real_services_(component::GetEnvironmentServices()) {}
  ~EnclosedGuest() = default;

  zx_status_t Start(fuchsia::guest::LaunchInfo guest_launch_info);

  void Stop() { loop_.Quit(); }

  zx_status_t Execute(std::string message, std::string* result = nullptr) {
    return serial_.ExecuteBlocking(message, result);
  }

  void GetHostVsockEndpoint(
      fidl::InterfaceRequest<fuchsia::guest::HostVsockEndpoint> endpoint) {
    environment_controller_->GetHostVsockEndpoint(std::move(endpoint));
  }

  uint32_t GetGuestCid() { return guest_cid_; }

 private:
  uint32_t guest_cid_;
  async::Loop loop_;
  std::shared_ptr<component::Services> real_services_;
  fuchsia::sys::EnvironmentPtr real_env_;
  std::unique_ptr<component::testing::EnclosingEnvironment>
      enclosing_environment_;
  fuchsia::guest::EnvironmentManagerPtr environment_manager_;
  fuchsia::guest::EnvironmentControllerPtr environment_controller_;
  fuchsia::guest::InstanceControllerPtr instance_controller_;
  TestSerial serial_;
};

#endif  // GARNET_BIN_GUEST_INTEGRATION_TESTS_ENCLOSED_GUEST_H_