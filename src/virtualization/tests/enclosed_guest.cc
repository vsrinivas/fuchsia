// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/tests/enclosed_guest.h"

#include <fuchsia/netstack/cpp/fidl.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/string_printf.h>

#include <algorithm>

#include "src/virtualization/tests/logger.h"

static constexpr char kGuestManagerUrl[] =
    "fuchsia-pkg://fuchsia.com/guest_manager#meta/guest_manager.cmx";
static constexpr char kRealm[] = "realmguestintegrationtest";
// TODO(MAC-229): Use consistent naming for the test utils here.
static constexpr char kFuchsiaTestUtilsUrl[] =
    "fuchsia-pkg://fuchsia.com/guest_integration_tests_utils";
static constexpr char kDebianTestUtilDir[] = "/test_utils";
static constexpr zx::duration kLoopTimeout = zx::sec(20);
static constexpr zx::duration kLoopConditionStep = zx::msec(10);
static constexpr size_t kNumRetries = 40;
static constexpr zx::duration kRetryStep = zx::msec(200);

static bool RunLoopUntil(async::Loop* loop, fit::function<bool()> condition) {
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

zx_status_t EnclosedGuest::Start() {
  Logger::Get().Reset();

  real_services_->Connect(real_env_.NewRequest());
  auto services =
      sys::testing::EnvironmentServices::Create(real_env_, loop_.dispatcher());

  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kGuestManagerUrl;
  zx_status_t status = services->AddServiceWithLaunchInfo(
      std::move(launch_info), fuchsia::guest::EnvironmentManager::Name_);
  if (status != ZX_OK) {
    return status;
  }

  status = services->AddService(mock_netstack_.GetHandler(),
                                fuchsia::netstack::Netstack::Name_);

  enclosing_environment_ = sys::testing::EnclosingEnvironment::Create(
      kRealm, real_env_, std::move(services));
  bool environment_running = RunLoopUntil(
      &loop_, [this] { return enclosing_environment_->is_running(); });
  if (!environment_running) {
    return ZX_ERR_BAD_STATE;
  }

  fuchsia::guest::LaunchInfo guest_launch_info;
  status = LaunchInfo(&guest_launch_info);
  if (status != ZX_OK) {
    return status;
  }

  // Generate an environment label from the URL, but remove path separator
  // characters which aren't allowed in the label.
  std::string env_label = guest_launch_info.url;
  std::replace(env_label.begin(), env_label.end(), '/', ':');

  enclosing_environment_->ConnectToService(environment_manager_.NewRequest());
  environment_manager_->Create(env_label, environment_controller_.NewRequest());

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
  status = serial_.Start(std::move(socket));
  if (status != ZX_OK) {
    return status;
  }

  status = WaitForSystemReady();
  if (status != ZX_OK) {
    return status;
  }

  ready_ = true;
  return ZX_OK;
}

zx_status_t ZirconEnclosedGuest::LaunchInfo(
    fuchsia::guest::LaunchInfo* launch_info) {
  launch_info->url = kZirconGuestUrl;
  launch_info->args.push_back("--virtio-gpu=false");
  launch_info->args.push_back("--cmdline-add=kernel.serial=none");
  return ZX_OK;
}

zx_status_t ZirconEnclosedGuest::WaitForSystemReady() {
  for (size_t i = 0; i != kNumRetries; ++i) {
    std::string ps;
    zx_status_t status = Execute("ps", &ps);
    if (status != ZX_OK) {
      continue;
    }
    auto appmgr = ps.find("appmgr");
    if (appmgr == std::string::npos) {
      zx::nanosleep(zx::deadline_after(kRetryStep));
      continue;
    }
    return ZX_OK;
  }
  FXL_LOG(ERROR) << "Failed to wait for appmgr";
  return ZX_ERR_TIMED_OUT;
}

zx_status_t ZirconEnclosedGuest::RunUtil(const std::string& util,
                                         const std::string& args,
                                         std::string* result) {
  std::string cmd =
      fxl::StringPrintf("/bin/run %s#meta/%s.cmx %s", kFuchsiaTestUtilsUrl,
                        util.c_str(), args.c_str());
  return Execute(cmd, result);
}

zx_status_t DebianEnclosedGuest::LaunchInfo(
    fuchsia::guest::LaunchInfo* launch_info) {
  launch_info->url = kDebianGuestUrl;
  launch_info->args.push_back("--virtio-gpu=false");
  return ZX_OK;
}

zx_status_t DebianEnclosedGuest::WaitForSystemReady() {
  for (size_t i = 0; i != kNumRetries; ++i) {
    std::string response;
    zx_status_t status = Execute("echo guest ready", &response);
    if (status != ZX_OK) {
      continue;
    }
    auto ready = response.find("guest ready");
    if (ready == std::string::npos) {
      zx::nanosleep(zx::deadline_after(kRetryStep));
      continue;
    }
    return ZX_OK;
  }
  FXL_LOG(ERROR) << "Failed to wait for shell";
  return ZX_ERR_TIMED_OUT;
}

zx_status_t DebianEnclosedGuest::RunUtil(const std::string& util,
                                         const std::string& args,
                                         std::string* result) {
  std::string cmd = fxl::StringPrintf("%s/%s %s", kDebianTestUtilDir,
                                      util.c_str(), args.c_str());
  return Execute(cmd, result);
}
