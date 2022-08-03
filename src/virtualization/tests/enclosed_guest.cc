// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/tests/enclosed_guest.h"

#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/kernel/cpp/fidl.h>
#include <fuchsia/net/virtualization/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/sysinfo/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fitx/result.h>
#include <lib/fpromise/single_threaded_executor.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <sys/mount.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>

#include "fuchsia/logger/cpp/fidl.h"
#include "fuchsia/virtualization/cpp/fidl.h"
#include "src/lib/files/file.h"
#include "src/lib/files/glob.h"
#include "src/lib/fxl/strings/ascii.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/virtualization/lib/grpc/grpc_vsock_stub.h"
#include "src/virtualization/lib/guest_config/guest_config.h"
#include "src/virtualization/tests/backtrace_watchdog.h"
#include "src/virtualization/tests/guest_constants.h"
#include "src/virtualization/tests/logger.h"
#include "src/virtualization/tests/periodic_logger.h"

using ::fuchsia::virtualization::HostVsockEndpoint_Listen_Result;
using ::fuchsia::virtualization::Listener;

namespace {

constexpr char kZirconGuestUrl[] =
    "fuchsia-pkg://fuchsia.com/zircon_guest_manager#meta/zircon_guest_manager.cm";
constexpr char kDebianGuestUrl[] =
    "fuchsia-pkg://fuchsia.com/debian_guest_manager#meta/debian_guest_manager.cm";
constexpr char kTerminaGuestUrl[] =
    "fuchsia-pkg://fuchsia.com/termina_guest_manager#meta/termina_guest_manager.cm";

// TODO(fxbug.dev/12589): Use consistent naming for the test utils here.
constexpr char kDebianTestUtilDir[] = "/test_utils";
constexpr zx::duration kLoopConditionStep = zx::msec(10);
constexpr zx::duration kRetryStep = zx::msec(200);
constexpr uint32_t kTerminaStartupListenerPort = 7777;
constexpr uint32_t kTerminaMaitredPort = 8888;

bool RunLoopUntil(async::Loop* loop, fit::function<bool()> condition, zx::time deadline) {
  while (zx::clock::get_monotonic() < deadline) {
    // Check our condition.
    if (condition()) {
      return true;
    }

    // Wait until next polling interval.
    loop->Run(zx::deadline_after(kLoopConditionStep));
    loop->ResetQuit();
  }

  return condition();
}

std::string JoinArgVector(const std::vector<std::string>& argv) {
  std::string result;
  for (const auto& arg : argv) {
    result += arg;
    result += " ";
  }
  return result;
}

}  // namespace

// Execute |command| on the guest serial and wait for the |result|.
zx_status_t EnclosedGuest::Execute(const std::vector<std::string>& argv,
                                   const std::unordered_map<std::string, std::string>& env,
                                   zx::time deadline, std::string* result, int32_t* return_code) {
  if (!env.empty()) {
    FX_LOGS(ERROR) << "Only TerminaEnclosedGuest::Execute accepts environment variables.";
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto command = JoinArgVector(argv);
  return console_->ExecuteBlocking(command, ShellPrompt(), deadline, result);
}

zx_status_t EnclosedGuest::Start(zx::time deadline) {
  using component_testing::RealmBuilder;
  using component_testing::RealmRoot;

  GuestLaunchInfo guest_launch_info;
  auto realm_builder = RealmBuilder::Create();
  if (auto status = InstallInRealm(realm_builder, guest_launch_info); status != ZX_OK) {
    return status;
  }

  realm_root_ = std::make_unique<RealmRoot>(realm_builder.Build(loop_.dispatcher()));
  return LaunchInRealm(*realm_root_, guest_launch_info, deadline);
}

zx_status_t EnclosedGuest::InstallInRealm(component_testing::RealmBuilder& realm_builder,
                                          GuestLaunchInfo& guest_launch_info) {
  using component_testing::ChildRef;
  using component_testing::Directory;
  using component_testing::ParentRef;
  using component_testing::Protocol;
  using component_testing::Route;

  constexpr auto kFakeNetstackComponentName = "fake_netstack";
  constexpr auto kFakeScenicComponentName = "fake_scenic";

  constexpr auto kDevGpuDirectory = "dev-gpu";
  constexpr auto kGuestManagerName = "guest_manager";

  zx_status_t status = LaunchInfo(&guest_launch_info);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failure launching guest image: ";
    return status;
  }

  realm_builder.AddChild(kGuestManagerName, guest_launch_info.url);
  realm_builder.AddLocalChild(kFakeNetstackComponentName, &fake_netstack_);
  realm_builder.AddLocalChild(kFakeScenicComponentName, &fake_scenic_);

  realm_builder
      .AddRoute(Route{.capabilities =
                          {
                              Protocol{fuchsia::logger::LogSink::Name_},
                              Protocol{fuchsia::kernel::HypervisorResource::Name_},
                              Protocol{fuchsia::kernel::VmexResource::Name_},
                              Protocol{fuchsia::sysinfo::SysInfo::Name_},
                              Protocol{fuchsia::sysmem::Allocator::Name_},
                              Protocol{fuchsia::tracing::provider::Registry::Name_},
                              Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                              Directory{.name = kDevGpuDirectory,
                                        .rights = fuchsia::io::R_STAR_DIR,
                                        .path = "/dev/class/gpu"},
                          },
                      .source = {ParentRef()},
                      .targets = {ChildRef{kGuestManagerName}}})
      .AddRoute(Route{.capabilities =
                          {
                              Protocol{fuchsia::net::virtualization::Control::Name_},
                          },
                      .source = {ChildRef{kFakeNetstackComponentName}},
                      .targets = {ChildRef{kGuestManagerName}}})
      .AddRoute(Route{.capabilities =
                          {
                              Protocol{fuchsia::ui::scenic::Scenic::Name_},
                          },
                      .source = {ChildRef{kFakeScenicComponentName}},
                      .targets = {ChildRef{kGuestManagerName}}})
      .AddRoute(Route{.capabilities =
                          {
                              Protocol{guest_launch_info.interface_name},
                          },
                      .source = ChildRef{kGuestManagerName},
                      .targets = {ParentRef()}});

  return ZX_OK;
}

zx_status_t EnclosedGuest::LaunchInRealm(const component_testing::RealmRoot& realm_root,
                                         GuestLaunchInfo& guest_launch_info, zx::time deadline) {
  Logger::Get().Reset();
  PeriodicLogger logger;

  fuchsia::virtualization::GuestManager_LaunchGuest_Result res;
  guest_manager_ = realm_root.ConnectSync<fuchsia::virtualization::GuestManager>(
      guest_launch_info.interface_name);

  auto status = SetupVsockServices(deadline, guest_launch_info);
  if (status != ZX_OK) {
    return status;
  }

  status =
      guest_manager_->LaunchGuest(std::move(guest_launch_info.config), guest_.NewRequest(), &res);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failure launching guest " << guest_launch_info.url;
    return status;
  }
  guest_cid_ = fuchsia::virtualization::DEFAULT_GUEST_CID;

  // TODO(fxbug.dev/97355): Get from Guest protocol instead of guest manager after migration.
  GetHostVsockEndpoint(vsock_.NewRequest());

  // Launch the guest.
  logger.Start("Launching guest", zx::sec(5));
  std::optional<zx_status_t> guest_error;
  guest_.set_error_handler([&guest_error](zx_status_t status) { guest_error = status; });

  // Connect to guest serial, and log it to the logger.
  logger.Start("Connecting to guest serial", zx::sec(10));
  std::optional<fuchsia::virtualization::Guest_GetSerial_Result> get_serial_result;

  guest_->GetSerial([&get_serial_result](fuchsia::virtualization::Guest_GetSerial_Result result) {
    get_serial_result = std::move(result);
  });

  bool success = RunLoopUntil(
      GetLoop(),
      [&guest_error, &get_serial_result] {
        return guest_error.has_value() || get_serial_result.has_value();
      },
      deadline);
  if (!success) {
    FX_LOGS(ERROR) << "Timed out waiting to connect to guest's serial";
    return ZX_ERR_TIMED_OUT;
  }
  if (guest_error.has_value()) {
    FX_LOGS(ERROR) << "Error connecting to guest's serial: "
                   << zx_status_get_string(guest_error.value());
    return guest_error.value();
  }

  if (get_serial_result->is_err()) {
    FX_PLOGS(ERROR, get_serial_result->err()) << "Failed to connect to guest's serial";
    return get_serial_result->err();
  }
  serial_logger_.emplace(&Logger::Get(), std::move(get_serial_result->response().socket));

  // Connect to guest console.
  logger.Start("Connecting to guest console", zx::sec(10));
  std::optional<fuchsia::virtualization::Guest_GetConsole_Result> get_console_result;
  guest_->GetConsole(
      [&get_console_result](fuchsia::virtualization::Guest_GetConsole_Result result) {
        get_console_result = std::move(result);
      });
  success = RunLoopUntil(
      GetLoop(),
      [&guest_error, &get_console_result] {
        return guest_error.has_value() || get_console_result.has_value();
      },
      deadline);
  if (!success) {
    FX_LOGS(ERROR) << "Timed out waiting to connect to guest's console";
    return ZX_ERR_TIMED_OUT;
  }
  if (guest_error.has_value()) {
    FX_LOGS(ERROR) << "Error connecting to guest's console: "
                   << zx_status_get_string(guest_error.value());
    return guest_error.value();
  }
  if (get_console_result->is_err()) {
    FX_PLOGS(ERROR, get_console_result->err()) << "Failed to open guest console";
    return get_console_result->err();
  }
  console_.emplace(std::make_unique<ZxSocket>(std::move(get_console_result->response().socket)));

  // Wait for output to appear on the console.
  logger.Start("Waiting for output to appear on guest console", zx::sec(10));
  status = console_->Start(deadline);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Error waiting for output on guest console: " << zx_status_get_string(status);
    return status;
  }

  // Poll the system for all services to come up.
  logger.Start("Waiting for system to become ready", zx::sec(10));
  status = WaitForSystemReady(deadline);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failure while waiting for guest system to become ready: "
                   << zx_status_get_string(status);
    return status;
  }

  return ZX_OK;
}

void EnclosedGuest::ConnectToBalloon(
    ::fidl::InterfaceRequest<::fuchsia::virtualization::BalloonController> controller) {
  guest_manager_->ConnectToBalloon(std::move(controller));
}

void EnclosedGuest::GetHostVsockEndpoint(
    ::fidl::InterfaceRequest<::fuchsia::virtualization::HostVsockEndpoint> endpoint) {
  guest_manager_->GetHostVsockEndpoint(std::move(endpoint));
}

zx_status_t EnclosedGuest::Stop(zx::time deadline) {
  zx_status_t status = ShutdownAndWait(deadline);
  if (status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

zx_status_t EnclosedGuest::RunUtil(const std::string& util, const std::vector<std::string>& argv,
                                   zx::time deadline, std::string* result) {
  return Execute(GetTestUtilCommand(util, argv), {}, deadline, result);
}

zx_status_t ZirconEnclosedGuest::LaunchInfo(GuestLaunchInfo* launch_info) {
  launch_info->url = kZirconGuestUrl;
  launch_info->interface_name = fuchsia::virtualization::ZirconGuestManager::Name_;
  // Disable netsvc to avoid spamming the net device with logs.
  launch_info->config.mutable_cmdline_add()->emplace_back("netsvc.disable=true");
  return ZX_OK;
}

namespace {
fitx::result<std::string> EnsureValidZirconPsOutput(std::string_view ps_output) {
  if (ps_output.find("appmgr") == std::string::npos) {
    return fitx::error("'appmgr' cannot be found in 'ps' output");
  }
  if (ps_output.find("virtual-console") == std::string::npos) {
    return fitx::error("'virtual-console' cannot be found in 'ps' output");
  }
  return fitx::ok();
}
}  // namespace

zx_status_t ZirconEnclosedGuest::WaitForSystemReady(zx::time deadline) {
  std::string ps;

  // Keep running `ps` until we get a reasonable result or run out of time.
  do {
    // Execute `ps`.
    zx_status_t status = Execute({"ps"}, {}, deadline, &ps);
    if (status != ZX_OK) {
      return status;
    }
    if (EnsureValidZirconPsOutput(ps).is_ok()) {
      return ZX_OK;
    }

    // Keep trying until we run out of time.
    zx::nanosleep(std::min(zx::deadline_after(kRetryStep), deadline));
  } while (zx::clock::get_monotonic() < deadline);

  FX_LOGS(ERROR) << "Failed to wait for appmgr and virtual-console: "
                 << EnsureValidZirconPsOutput(ps).error_value();
  return ZX_ERR_TIMED_OUT;
}

zx_status_t ZirconEnclosedGuest::ShutdownAndWait(zx::time deadline) {
  std::optional<GuestConsole>& console_opt = GetConsole();
  if (console_opt.has_value()) {
    GuestConsole& console = console_opt.value();
    zx_status_t status = console.SendBlocking("dm shutdown\n", deadline);
    if (status != ZX_OK) {
      return status;
    }
    return console.WaitForSocketClosed(deadline);
  }
  return ZX_OK;
}

std::vector<std::string> ZirconEnclosedGuest::GetTestUtilCommand(
    const std::string& util, const std::vector<std::string>& argv) {
  std::vector<std::string> exec_argv = {util};
  exec_argv.insert(exec_argv.end(), argv.begin(), argv.end());
  return exec_argv;
}

zx_status_t DebianEnclosedGuest::LaunchInfo(GuestLaunchInfo* launch_info) {
  launch_info->url = kDebianGuestUrl;
  launch_info->interface_name = fuchsia::virtualization::DebianGuestManager::Name_;
  // Enable kernel debugging serial output.
  for (std::string_view cmd : kLinuxKernelSerialDebugCmdline) {
    launch_info->config.mutable_cmdline_add()->emplace_back(cmd);
  }
  return ZX_OK;
}

zx_status_t DebianEnclosedGuest::WaitForSystemReady(zx::time deadline) {
  std::optional<GuestConsole>& console_opt = GetConsole();
  if (console_opt.has_value()) {
    GuestConsole& console = console_opt.value();
    constexpr zx::duration kEchoWaitTime = zx::sec(1);
    return console.RepeatCommandTillSuccess("echo guest ready", ShellPrompt(), "guest ready",
                                            deadline, kEchoWaitTime);
  } else {
    return ZX_ERR_BAD_STATE;
  }
}

zx_status_t DebianEnclosedGuest::ShutdownAndWait(zx::time deadline) {
  PeriodicLogger logger("Attempting to shut down guest", zx::sec(10));
  std::optional<GuestConsole>& console_opt = GetConsole();
  if (console_opt.has_value()) {
    GuestConsole& console = console_opt.value();
    zx_status_t status = console.SendBlocking("shutdown now\n", deadline);
    if (status != ZX_OK) {
      return status;
    }
    return console.WaitForSocketClosed(deadline);
  }
  return ZX_OK;
}

std::vector<std::string> DebianEnclosedGuest::GetTestUtilCommand(
    const std::string& util, const std::vector<std::string>& argv) {
  std::string bin_path = fxl::StringPrintf("%s/%s", kDebianTestUtilDir, util.c_str());

  std::vector<std::string> exec_argv = {bin_path};
  exec_argv.insert(exec_argv.end(), argv.begin(), argv.end());
  return exec_argv;
}

zx_status_t TerminaEnclosedGuest::LaunchInfo(GuestLaunchInfo* launch_info) {
  launch_info->url = kTerminaGuestUrl;
  launch_info->interface_name = fuchsia::virtualization::TerminaGuestManager::Name_;
  launch_info->config.set_virtio_gpu(false);
  launch_info->config.set_magma_device(fuchsia::virtualization::MagmaDevice());

  // Add the block device that contains the VM extras
  {
    fbl::unique_fd fd(open("/pkg/data/vm_extras.img", O_RDONLY));
    if (!fd.is_valid()) {
      return ZX_ERR_BAD_STATE;
    }
    zx::channel client;
    zx_status_t status = fdio_get_service_handle(fd.get(), client.reset_and_get_address());
    if (status != ZX_OK) {
      return status;
    }
    launch_info->config.mutable_block_devices()->push_back({
        "vm_extras",
        fuchsia::virtualization::BlockMode::READ_ONLY,
        fuchsia::virtualization::BlockFormat::FILE,
        std::move(client),
    });
  }
  // Add the block device that contains the test binaries.
  {
    fbl::unique_fd fd(open("/pkg/data/linux_tests.img", O_RDONLY));
    if (!fd.is_valid()) {
      return ZX_ERR_BAD_STATE;
    }
    zx::channel client;
    zx_status_t status = fdio_get_service_handle(fd.get(), client.reset_and_get_address());
    if (status != ZX_OK) {
      return status;
    }
    launch_info->config.mutable_block_devices()->push_back({
        "linux_tests",
        fuchsia::virtualization::BlockMode::READ_ONLY,
        fuchsia::virtualization::BlockFormat::FILE,
        std::move(client),
    });
  }
  {
    // Add non-prebuilt test extras.
    fbl::unique_fd fd(open("/pkg/data/extras.img", O_RDONLY));
    if (!fd.is_valid()) {
      return ZX_ERR_BAD_STATE;
    }
    zx::channel client;
    zx_status_t status = fdio_get_service_handle(fd.get(), client.reset_and_get_address());
    if (status != ZX_OK) {
      return status;
    }
    launch_info->config.mutable_block_devices()->push_back({
        "extras",
        fuchsia::virtualization::BlockMode::READ_ONLY,
        fuchsia::virtualization::BlockFormat::FILE,
        std::move(client),
    });
  }

  // Enable kernel debugging serial output.
  for (std::string_view cmd : kLinuxKernelSerialDebugCmdline) {
    launch_info->config.mutable_cmdline_add()->emplace_back(cmd);
  }

  return ZX_OK;
}

zx_status_t TerminaEnclosedGuest::SetupVsockServices(zx::time deadline,
                                                     GuestLaunchInfo& guest_launch_info) {
  GrpcVsockServerBuilder builder;
  builder.AddListenPort(kTerminaStartupListenerPort);
  builder.RegisterService(this);

  std::vector<Listener> listeners;
  executor_.schedule_task(builder.Build().and_then(
      [this, &listeners](
          std::pair<std::unique_ptr<GrpcVsockServer>, std::vector<Listener>>& args) mutable {
        server_ = std::move(args.first);
        listeners = std::move(args.second);
        return fpromise::ok();
      }));
  if (!RunLoopUntil(
          GetLoop(), [this] { return server_ != nullptr; }, deadline)) {
    return ZX_ERR_TIMED_OUT;
  }

  for (auto& listener : listeners) {
    guest_launch_info.config.mutable_vsock_listeners()->push_back(std::move(listener));
  }

  return ZX_OK;
}

grpc::Status TerminaEnclosedGuest::VmReady(grpc::ServerContext* context,
                                           const vm_tools::EmptyMessage* request,
                                           vm_tools::EmptyMessage* response) {
  auto p = NewGrpcVsockStub<vm_tools::Maitred>(vsock_, kTerminaMaitredPort);
  auto result = fpromise::run_single_threaded(std::move(p));
  if (result.is_ok()) {
    maitred_ = std::move(result.value());
  } else {
    FX_PLOGS(ERROR, result.error()) << "Failed to connect to maitred";
  }
  return grpc::Status::OK;
}

// Use Maitred to mount the given block device at the given location.
//
// The destination directory will be created if required.
zx_status_t MountDeviceInGuest(vm_tools::Maitred::Stub& maitred, std::string_view block_device,
                               std::string_view mount_point, std::string_view fs_type,
                               uint64_t mount_flags) {
  grpc::ClientContext context;
  vm_tools::MountRequest request;
  vm_tools::MountResponse response;

  request.mutable_source()->assign(block_device);
  request.mutable_target()->assign(mount_point);
  request.mutable_fstype()->assign(fs_type);
  request.set_mountflags(mount_flags);
  request.set_create_target(true);

  auto grpc_status = maitred.Mount(&context, request, &response);
  if (!grpc_status.ok()) {
    FX_LOGS(ERROR) << "Request to mount block device '" << block_device
                   << "' failed: " << grpc_status.error_message();
    return ZX_ERR_IO;
  }
  if (response.error() != 0) {
    FX_LOGS(ERROR) << "Mounting block device '" << block_device << "' failed: " << response.error();
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

zx_status_t TerminaEnclosedGuest::WaitForSystemReady(zx::time deadline) {
  // The VM will connect to the StartupListener port when it's ready and we'll
  // create the maitred stub in |VmReady|.
  {
    PeriodicLogger logger("Wait for maitred", zx::sec(1));
    if (!RunLoopUntil(
            GetLoop(), [this] { return maitred_ != nullptr; }, deadline)) {
      return ZX_ERR_TIMED_OUT;
    }
  }
  FX_CHECK(maitred_) << "No maitred connection";

  // Connect to vshd.
  fuchsia::virtualization::HostVsockEndpointPtr endpoint;
  GetHostVsockEndpoint(endpoint.NewRequest());

  command_runner_ = std::make_unique<vsh::BlockingCommandRunner>(std::move(endpoint));

  // Create mountpoints for test utils and extras. The root filesystem is read only so we
  // put these under /tmp.
  zx_status_t status;
  status = MountDeviceInGuest(*maitred_, "/dev/vdc", "/tmp/vm_extras", "ext2", MS_RDONLY);
  if (status != ZX_OK) {
    return status;
  }
  status = MountDeviceInGuest(*maitred_, "/dev/vdd", "/tmp/test_utils", "romfs", MS_RDONLY);
  if (status != ZX_OK) {
    return status;
  }
  status = MountDeviceInGuest(*maitred_, "/dev/vde", "/tmp/extras", "romfs", MS_RDONLY);
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

zx_status_t TerminaEnclosedGuest::ShutdownAndWait(zx::time deadline) {
  if (server_) {
    server_->inner()->Shutdown();
    server_->inner()->Wait();
  }
  return ZX_OK;
}

zx_status_t TerminaEnclosedGuest::Execute(const std::vector<std::string>& command,
                                          const std::unordered_map<std::string, std::string>& env,
                                          zx::time deadline, std::string* result,
                                          int32_t* return_code) {
  std::vector<std::string> argv = {"sh", "-c", JoinArgVector(command)};
  auto command_result = command_runner_->Execute({argv, env});
  if (command_result.is_error()) {
    return command_result.error();
  }
  if (result) {
    *result = std::move(command_result.value().out);
    if (!command_result.value().err.empty()) {
      *result += "\n";
      *result += command_result.value().err;
    }
  }
  if (return_code) {
    *return_code = command_result.value().return_code;
  }
  return ZX_OK;
}

std::vector<std::string> TerminaEnclosedGuest::GetTestUtilCommand(
    const std::string& util, const std::vector<std::string>& argv) {
  std::vector<std::string> final_argv;
  final_argv.emplace_back("/tmp/test_utils/" + util);
  final_argv.insert(final_argv.end(), argv.begin(), argv.end());
  return final_argv;
}
