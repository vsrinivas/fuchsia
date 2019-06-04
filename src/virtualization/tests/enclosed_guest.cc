// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/tests/enclosed_guest.h"

#include <fuchsia/netstack/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
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
static constexpr zx::duration kLoopTimeout = zx::sec(300);
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

static std::string JoinArgVector(const std::vector<std::string>& argv) {
  std::string result;
  for (const auto& arg : argv) {
    result += arg;
    result += " ";
  }
  return result;
}

// Execute |command| on the guest serial and wait for the |result|.
zx_status_t EnclosedGuest::Execute(const std::vector<std::string>& argv,
                                   std::string* result) {
  auto command = JoinArgVector(argv);
  return console_->ExecuteBlocking(command, ShellPrompt(), result);
}

zx_status_t EnclosedGuest::Start() {
  Logger::Get().Reset();

  real_services_->Connect(real_env_.NewRequest());
  auto services =
      sys::testing::EnvironmentServices::Create(real_env_, loop_.dispatcher());

  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kGuestManagerUrl;
  zx_status_t status = services->AddServiceWithLaunchInfo(
      std::move(launch_info), fuchsia::virtualization::Manager::Name_);
  if (status != ZX_OK) {
    return status;
  }

  status = services->AddService(mock_netstack_.GetHandler(),
                                fuchsia::netstack::Netstack::Name_);
  if (status != ZX_OK) {
    return status;
  }

  status = services->AddService(fake_scenic_.GetHandler(),
                                fuchsia::ui::scenic::Scenic::Name_);
  if (status != ZX_OK) {
    return status;
  }

  enclosing_environment_ = sys::testing::EnclosingEnvironment::Create(
      kRealm, real_env_, std::move(services));
  bool environment_running = RunLoopUntil(
      &loop_, [this] { return enclosing_environment_->is_running(); });
  if (!environment_running) {
    return ZX_ERR_BAD_STATE;
  }

  fuchsia::virtualization::LaunchInfo guest_launch_info;
  status = LaunchInfo(&guest_launch_info);
  if (status != ZX_OK) {
    return status;
  }

  // Generate an environment label from the URL, but remove path separator
  // characters which aren't allowed in the label.
  std::string env_label = guest_launch_info.url;
  std::replace(env_label.begin(), env_label.end(), '/', ':');

  enclosing_environment_->ConnectToService(manager_.NewRequest());
  manager_->Create(env_label, realm_.NewRequest());

  realm_->LaunchInstance(std::move(guest_launch_info), guest_.NewRequest(),
                         [this](uint32_t cid) {
                           guest_cid_ = cid;
                           loop_.Quit();
                         });
  loop_.Run();

  zx::socket serial_socket;
  guest_->GetSerial(
      [&serial_socket](zx::socket s) { serial_socket = std::move(s); });
  bool socket_valid =
      RunLoopUntil(&loop_, [&] { return serial_socket.is_valid(); });
  if (!socket_valid) {
    return ZX_ERR_BAD_STATE;
  }
  console_ = std::make_unique<GuestConsole>(
      std::make_unique<ZxSocket>(std::move(serial_socket)));
  status = console_->Start();
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

zx_status_t EnclosedGuest::RunUtil(const std::string& util,
                                   const std::vector<std::string>& argv,
                                   std::string* result) {
  return Execute(GetTestUtilCommand(util, argv), result);
}

zx_status_t ZirconEnclosedGuest::LaunchInfo(
    fuchsia::virtualization::LaunchInfo* launch_info) {
  launch_info->url = kZirconGuestUrl;
  launch_info->args.push_back("--cmdline-add=kernel.serial=none");
  return ZX_OK;
}

zx_status_t ZirconEnclosedGuest::WaitForSystemReady() {
  for (size_t i = 0; i != kNumRetries; ++i) {
    std::string ps;
    zx_status_t status = Execute({"ps"}, &ps);
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

std::vector<std::string> ZirconEnclosedGuest::GetTestUtilCommand(
    const std::string& util, const std::vector<std::string>& argv) {
  std::string fuchsia_url =
      fxl::StringPrintf("%s#meta/%s.cmx", kFuchsiaTestUtilsUrl, util.c_str());
  std::vector<std::string> exec_argv = {"/bin/run", fuchsia_url};
  exec_argv.insert(exec_argv.end(), argv.begin(), argv.end());
  return exec_argv;
}

zx_status_t DebianEnclosedGuest::LaunchInfo(
    fuchsia::virtualization::LaunchInfo* launch_info) {
  launch_info->url = kDebianGuestUrl;
  return ZX_OK;
}

zx_status_t DebianEnclosedGuest::WaitForSystemReady() {
  for (size_t i = 0; i != kNumRetries; ++i) {
    std::string response;
    zx_status_t status = Execute({"echo", "guest ready"}, &response);
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

std::vector<std::string> DebianEnclosedGuest::GetTestUtilCommand(
    const std::string& util, const std::vector<std::string>& argv) {
  std::string bin_path =
      fxl::StringPrintf("%s/%s", kDebianTestUtilDir, util.c_str());

  std::vector<std::string> exec_argv = {bin_path};
  exec_argv.insert(exec_argv.end(), argv.begin(), argv.end());
  return exec_argv;
}
