// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/tests/enclosed_guest.h"

#include <fcntl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <fuchsia/sysinfo/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fit/single_threaded_executor.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <string.h>
#include <sys/mount.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <algorithm>
#include <optional>
#include <string>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/virtualization/bin/vmm/guest_config.h"
#include "src/virtualization/lib/grpc/grpc_vsock_stub.h"
#include "src/virtualization/tests/logger.h"
#include "src/virtualization/tests/periodic_logger.h"

static constexpr char kGuestManagerUrl[] =
    "fuchsia-pkg://fuchsia.com/guest_manager#meta/guest_manager.cmx";
static constexpr char kRealm[] = "realmguestintegrationtest";
// TODO(fxbug.dev/12589): Use consistent naming for the test utils here.
static constexpr char kFuchsiaTestUtilsUrl[] =
    "fuchsia-pkg://fuchsia.com/virtualization-test-utils";
static constexpr char kDebianTestUtilDir[] = "/test_utils";
static constexpr zx::duration kLoopTimeout = zx::sec(300);
static constexpr zx::duration kLoopConditionStep = zx::msec(10);
static constexpr size_t kNumRetries = 40;
static constexpr zx::duration kRetryStep = zx::msec(200);
static constexpr uint32_t kTerminaStartupListenerPort = 7777;
static constexpr uint32_t kTerminaMaitredPort = 8888;

static bool RunLoopUntil(async::Loop* loop, fit::function<bool()> condition,
                         std::optional<PeriodicLogger> logger = std::nullopt) {
  const zx::time deadline = zx::deadline_after(kLoopTimeout);
  while (zx::clock::get_monotonic() < deadline) {
    // Check our condition.
    if (condition()) {
      return true;
    }

    // If we have been polling for long enough, print a log message.
    if (logger) {
      logger->LogIfRequired();
    }

    // Wait until next polling interval.
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
                                   const std::unordered_map<std::string, std::string>& env,
                                   std::string* result, int32_t* return_code) {
  if (env.size() > 0) {
    FX_LOGS(ERROR) << "Only TerminaEnclosedGuest::Execute accepts environment variables.";
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto command = JoinArgVector(argv);
  return console_->ExecuteBlocking(command, ShellPrompt(), result);
}

zx_status_t EnclosedGuest::Start() {
  Logger::Get().Reset();

  real_services_->Connect(real_env_.NewRequest());
  auto services = sys::testing::EnvironmentServices::Create(real_env_, loop_.dispatcher());

  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kGuestManagerUrl;
  zx_status_t status = services->AddServiceWithLaunchInfo(std::move(launch_info),
                                                          fuchsia::virtualization::Manager::Name_);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failure launching virtualization manager: " << zx_status_get_string(status);
    return status;
  }

  status = services->AddService(fake_netstack_.GetHandler(), fuchsia::netstack::Netstack::Name_);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failure launching mock netstack: " << zx_status_get_string(status);
    return status;
  }

  status = services->AddService(fake_scenic_.GetHandler(), fuchsia::ui::scenic::Scenic::Name_);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failure launching fake scenic service: " << zx_status_get_string(status);
    return status;
  }

  status = services->AllowParentService(fuchsia::sysinfo::SysInfo::Name_);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failure adding sysinfo service: " << zx_status_get_string(status);
    return status;
  }

  enclosing_environment_ =
      sys::testing::EnclosingEnvironment::Create(kRealm, real_env_, std::move(services));
  bool environment_running = RunLoopUntil(
      &loop_, [this] { return enclosing_environment_->is_running(); },
      PeriodicLogger("Creating guest sandbox", zx::sec(10)));
  if (!environment_running) {
    FX_LOGS(ERROR) << "Timed out waiting for guest sandbox environment to become ready.";
    return ZX_ERR_TIMED_OUT;
  }

  fuchsia::virtualization::LaunchInfo guest_launch_info;
  status = LaunchInfo(&guest_launch_info);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failure launching guest image: " << zx_status_get_string(status);
    return status;
  }

  // Generate an environment label from the URL, but remove path separator
  // characters which aren't allowed in the label.
  std::string env_label = guest_launch_info.url;
  std::replace(env_label.begin(), env_label.end(), '/', ':');

  enclosing_environment_->ConnectToService(manager_.NewRequest());
  manager_->Create(env_label, realm_.NewRequest());

  status = SetupVsockServices();
  if (status != ZX_OK) {
    return status;
  }

  // Launch the guest.
  bool launch_complete = false;
  realm_->LaunchInstance(std::move(guest_launch_info), guest_.NewRequest(),
                         [this, &launch_complete](uint32_t cid) {
                           guest_cid_ = cid;
                           launch_complete = true;
                         });
  RunLoopUntil(
      &loop_, [&launch_complete]() { return launch_complete; },
      PeriodicLogger("Launching guest", zx::sec(10)));

  zx::socket serial_socket;
  guest_->GetSerial([&serial_socket](zx::socket s) { serial_socket = std::move(s); });
  bool socket_valid = RunLoopUntil(
      &loop_, [&serial_socket] { return serial_socket.is_valid(); },
      PeriodicLogger("Connecting to guest serial", zx::sec(10)));
  if (!socket_valid) {
    FX_LOGS(ERROR) << "Timed out waiting to connect to guest's serial.";
    return ZX_ERR_TIMED_OUT;
  }
  console_ = std::make_unique<GuestConsole>(std::make_unique<ZxSocket>(std::move(serial_socket)));
  status = console_->Start();
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Error connecting to guest's console: " << zx_status_get_string(status);
    return status;
  }

  status = WaitForSystemReady();
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failure while waiting for guest system to become ready: "
                   << zx_status_get_string(status);
    return status;
  }

  ready_ = true;
  return ZX_OK;
}

zx_status_t EnclosedGuest::RunUtil(const std::string& util, const std::vector<std::string>& argv,
                                   std::string* result) {
  return Execute(GetTestUtilCommand(util, argv), {}, result);
}

zx_status_t ZirconEnclosedGuest::LaunchInfo(fuchsia::virtualization::LaunchInfo* launch_info) {
  launch_info->url = kZirconGuestUrl;
  launch_info->guest_config.mutable_cmdline_add()->push_back("kernel.serial=none");
  launch_info->guest_config.set_virtio_magma(false);
  return ZX_OK;
}

zx_status_t ZirconEnclosedGuest::WaitForSystemReady() {
  PeriodicLogger logger{"Waiting for guest system shell", zx::sec(10)};
  for (size_t i = 0; i != kNumRetries; ++i) {
    logger.LogIfRequired();
    std::string ps;
    zx_status_t status = Execute({"ps"}, {}, &ps);
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
  FX_LOGS(ERROR) << "Failed to wait for appmgr";
  return ZX_ERR_TIMED_OUT;
}

std::vector<std::string> ZirconEnclosedGuest::GetTestUtilCommand(
    const std::string& util, const std::vector<std::string>& argv) {
  std::string fuchsia_url = fxl::StringPrintf("%s#meta/%s.cmx", kFuchsiaTestUtilsUrl, util.c_str());
  std::vector<std::string> exec_argv = {"/bin/run", fuchsia_url};
  exec_argv.insert(exec_argv.end(), argv.begin(), argv.end());
  return exec_argv;
}

zx_status_t DebianEnclosedGuest::LaunchInfo(fuchsia::virtualization::LaunchInfo* launch_info) {
  launch_info->url = kDebianGuestUrl;
  launch_info->guest_config.set_virtio_magma(false);
  return ZX_OK;
}

zx_status_t DebianEnclosedGuest::WaitForSystemReady() {
  PeriodicLogger logger{"Waiting for guest system shell", zx::sec(10)};
  for (size_t i = 0; i != kNumRetries; ++i) {
    logger.LogIfRequired();
    std::string response;
    zx_status_t status = Execute({"echo", "guest ready"}, {}, &response);
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
  FX_LOGS(ERROR) << "Failed to wait for shell";
  return ZX_ERR_TIMED_OUT;
}

std::vector<std::string> DebianEnclosedGuest::GetTestUtilCommand(
    const std::string& util, const std::vector<std::string>& argv) {
  std::string bin_path = fxl::StringPrintf("%s/%s", kDebianTestUtilDir, util.c_str());

  std::vector<std::string> exec_argv = {bin_path};
  exec_argv.insert(exec_argv.end(), argv.begin(), argv.end());
  return exec_argv;
}

zx_status_t TerminaEnclosedGuest::LaunchInfo(fuchsia::virtualization::LaunchInfo* launch_info) {
  launch_info->url = kTerminaGuestUrl;
  launch_info->guest_config.set_virtio_gpu(false);

  // Add the block device that contains the test binaries.
  int fd = open("/pkg/data/linux_tests.img", O_RDONLY);
  if (fd < 0) {
    return ZX_ERR_BAD_STATE;
  }
  zx_handle_t handle;
  zx_status_t status = fdio_get_service_handle(fd, &handle);
  if (status != ZX_OK) {
    return status;
  }
  launch_info->block_devices.emplace();
  launch_info->block_devices->push_back({
      "linux_tests",
      fuchsia::virtualization::BlockMode::READ_ONLY,
      fuchsia::virtualization::BlockFormat::RAW,
      fidl::InterfaceHandle<fuchsia::io::File>(zx::channel(handle)),
  });
  // Add non-prebuilt test extras.
  fd = open("/pkg/data/extras.img", O_RDONLY);
  if (fd < 0) {
    return ZX_ERR_BAD_STATE;
  }
  status = fdio_get_service_handle(fd, &handle);
  if (status != ZX_OK) {
    return status;
  }
  launch_info->block_devices->push_back({
      "extras",
      fuchsia::virtualization::BlockMode::READ_ONLY,
      fuchsia::virtualization::BlockFormat::RAW,
      fidl::InterfaceHandle<fuchsia::io::File>(zx::channel(handle)),
  });
  return ZX_OK;
}

zx_status_t TerminaEnclosedGuest::SetupVsockServices() {
  fuchsia::virtualization::HostVsockEndpointPtr grpc_endpoint;
  GetHostVsockEndpoint(vsock_.NewRequest());
  GetHostVsockEndpoint(grpc_endpoint.NewRequest());

  GrpcVsockServerBuilder builder(std::move(grpc_endpoint));
  builder.AddListenPort(kTerminaStartupListenerPort);
  builder.RegisterService(this);

  executor_.schedule_task(
      builder.Build().and_then([this](std::unique_ptr<GrpcVsockServer>& result) mutable {
        server_ = std::move(result);
        return fit::ok();
      }));
  if (!RunLoopUntil(&loop_, [this] { return server_ != nullptr; })) {
    return ZX_ERR_TIMED_OUT;
  }

  return ZX_OK;
}

grpc::Status TerminaEnclosedGuest::VmReady(grpc::ServerContext* context,
                                           const vm_tools::EmptyMessage* request,
                                           vm_tools::EmptyMessage* response) {
  auto p = NewGrpcVsockStub<vm_tools::Maitred>(vsock_, GetGuestCid(), kTerminaMaitredPort);
  auto result = fit::run_single_threaded(std::move(p));
  if (result.is_ok()) {
    maitred_ = std::move(result.value());
  } else {
    FX_PLOGS(ERROR, result.error()) << "Failed to connect to maitred";
  }
  return grpc::Status::OK;
}

zx_status_t TerminaEnclosedGuest::WaitForSystemReady() {
  // The VM will connect to the StartupListener port when it's ready and we'll
  // create the maitred stub in |VmReady|.
  if (!RunLoopUntil(
          &loop_, [this] { return maitred_ != nullptr; },
          PeriodicLogger("Wait for maitred", zx::sec(1)))) {
    return ZX_ERR_TIMED_OUT;
  }
  FX_CHECK(maitred_) << "No maitred connection";

  // Connect to vshd.
  fuchsia::virtualization::HostVsockEndpointPtr endpoint;
  GetHostVsockEndpoint(endpoint.NewRequest());
  command_runner_ =
      std::make_unique<vsh::BlockingCommandRunner>(std::move(endpoint), GetGuestCid());

  // Create mountpoints for test utils and extras. The root filesystem is read only so we
  // put these under /tmp.
  {
    for (auto dir : {"/tmp/test_utils", "/tmp/extras"}) {
      auto command_result = command_runner_->Execute({
          {"mkdir", "-p", dir},
          {},
      });
      if (!command_result.is_ok()) {
        FX_LOGS(ERROR) << "Command fails with error "
                       << zx_status_get_string(command_result.error());
        return command_result.error();
      }
    }
  }

  // Mount the test_utils disk image.
  {
    grpc::ClientContext context;
    vm_tools::MountRequest request;
    vm_tools::MountResponse response;

    request.mutable_source()->assign("/dev/vdb");
    request.mutable_target()->assign("/tmp/test_utils");
    request.mutable_fstype()->assign("ext2");
    request.set_mountflags(MS_RDONLY);

    auto grpc_status = maitred_->Mount(&context, request, &response);
    if (!grpc_status.ok()) {
      FX_LOGS(ERROR) << "Failed to mount test_utils filesystem: " << grpc_status.error_message();
      return ZX_ERR_IO;
    }
    if (response.error() != 0) {
      FX_LOGS(ERROR) << "test_utils mount failed: " << response.error();
      return ZX_ERR_IO;
    }
  }

  // Mount the extras disk image.
  {
    grpc::ClientContext context;
    vm_tools::MountRequest request;
    vm_tools::MountResponse response;

    request.mutable_source()->assign("/dev/vdc");
    request.mutable_target()->assign("/tmp/extras");
    request.mutable_fstype()->assign("romfs");
    request.set_mountflags(MS_RDONLY);

    auto grpc_status = maitred_->Mount(&context, request, &response);
    if (!grpc_status.ok()) {
      FX_LOGS(ERROR) << "Failed to mount extras filesystem: " << grpc_status.error_message();
      return ZX_ERR_IO;
    }
    if (response.error() != 0) {
      FX_LOGS(ERROR) << "extras mount failed: " << response.error();
      return ZX_ERR_IO;
    }
  }

  return ZX_OK;
}

void TerminaEnclosedGuest::WaitForSystemStopped() {
  if (server_) {
    server_->inner()->Shutdown();
    server_->inner()->Wait();
  }
}

zx_status_t TerminaEnclosedGuest::Execute(const std::vector<std::string>& argv,
                                          const std::unordered_map<std::string, std::string>& env,
                                          std::string* result, int32_t* return_code) {
  auto command_result = command_runner_->Execute({argv, env});
  if (command_result.is_error()) {
    return command_result.error();
  }
  if (result) {
    *result = std::move(command_result.value().out);
    if (!command_result.value().err.empty()) {
      *result += "\n";
      *result += std::move(command_result.value().err);
    }
  }
  if (return_code) {
    *return_code = command_result.value().return_code;
  }
  return ZX_OK;
}

std::vector<std::string> TerminaEnclosedGuest::GetTestUtilCommand(
    const std::string& util, const std::vector<std::string>& args) {
  std::vector<std::string> argv;
  argv.emplace_back("/tmp/test_utils/" + util);
  argv.insert(argv.end(), args.begin(), args.end());
  return argv;
}
