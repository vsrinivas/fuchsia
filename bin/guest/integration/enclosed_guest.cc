// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/integration/enclosed_guest.h"

static constexpr char kGuestMgrUrl[] = "guestmgr";
static constexpr char kRealm[] = "realmguestintegrationtest";
static constexpr zx::duration kLoopTimeout = zx::sec(5);
static constexpr zx::duration kLoopConditionStep = zx::msec(10);

namespace {

bool RunLoopUntil(async::Loop* loop, fit::function<bool()> condition) {
  const zx::time deadline = zx::deadline_after(kLoopTimeout);
  while (zx::clock::get_monotonic() < deadline) {
    if (condition()) {
      return true;
    }
    loop->Run(zx::deadline_after(kLoopConditionStep));
    loop->ResetQuit();
  }
  return condition();
}

}  // namespace

zx_status_t EnclosedGuest::Start(fuchsia::guest::LaunchInfo guest_launch_info) {
  real_services_->ConnectToService(real_env_.NewRequest());
  auto services = component::testing::EnvironmentServices::Create(
      real_env_, loop_.dispatcher());
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kGuestMgrUrl;
  zx_status_t status = services->AddServiceWithLaunchInfo(
      std::move(launch_info), fuchsia::guest::EnvironmentManager::Name_);
  if (status != ZX_OK) {
    return status;
  }

  enclosing_environment_ = component::testing::EnclosingEnvironment::Create(
      kRealm, real_env_, std::move(services));
  bool environment_running = RunLoopUntil(
      &loop_, [this] { return enclosing_environment_->is_running(); });
  if (!environment_running) {
    return ZX_ERR_BAD_STATE;
  }

  enclosing_environment_->ConnectToService(environment_manager_.NewRequest());
  environment_manager_->Create(guest_launch_info.url,
                               environment_controller_.NewRequest());

  environment_controller_->LaunchInstance(std::move(guest_launch_info),
                                          instance_controller_.NewRequest(),
                                          [this](uint32_t cid) {
                                            guest_cid_ = cid;
                                            loop_.Quit();
                                          });
  loop_.Run();

  zx::socket socket;
  instance_controller_->GetSerial(
      [&socket](zx::socket s) { socket = std::move(s); });
  bool socket_valid = RunLoopUntil(&loop_, [&] { return socket.is_valid(); });
  if (!socket_valid) {
    return ZX_ERR_BAD_STATE;
  }
  return serial_.Start(std::move(socket));
}
