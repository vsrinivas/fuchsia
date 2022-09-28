// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/tests/lib/enclosed_guest.h"

#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/element/cpp/fidl.h>
#include <fuchsia/kernel/cpp/fidl.h>
#include <fuchsia/net/virtualization/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/sysinfo/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/input3/cpp/fidl.h>
#include <fuchsia/ui/observation/geometry/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
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
#include "src/virtualization/tests/lib/backtrace_watchdog.h"
#include "src/virtualization/tests/lib/guest_constants.h"
#include "src/virtualization/tests/lib/logger.h"
#include "src/virtualization/tests/lib/periodic_logger.h"

using ::fuchsia::virtualization::HostVsockEndpoint_Listen_Result;
using ::fuchsia::virtualization::Listener;

namespace {

using fuchsia::ui::observation::geometry::ViewDescriptor;

constexpr char kZirconGuestUrl[] =
    "fuchsia-pkg://fuchsia.com/zircon_guest_manager#meta/zircon_guest_manager.cm";
constexpr char kDebianGuestUrl[] =
    "fuchsia-pkg://fuchsia.com/debian_guest_manager#meta/debian_guest_manager.cm";
constexpr char kTerminaGuestUrl[] = "#meta/termina_guest_manager.cm";
constexpr auto kDevGpuDirectory = "dev-gpu";
constexpr auto kGuestManagerName = "guest_manager";

// TODO(fxbug.dev/12589): Use consistent naming for the test utils here.
constexpr char kDebianTestUtilDir[] = "/test_utils";
constexpr zx::duration kLoopConditionStep = zx::msec(10);
constexpr zx::duration kRetryStep = zx::msec(200);

std::string JoinArgVector(const std::vector<std::string>& argv) {
  std::string result;
  for (const auto& arg : argv) {
    result += arg;
    result += " ";
  }
  return result;
}

void InstallTestGraphicalPresenter(component_testing::Realm& realm) {
  using component_testing::ChildRef;
  using component_testing::ParentRef;
  using component_testing::Protocol;
  using component_testing::Route;

  // UITestRealm does not currently provide a fuchsia.element.GraphicalPresenter, but the
  // test_graphical_presenter exposes a ViewProvider and a GraphicalPresenter. We will connect this
  // to the UITestRealm such that our view under test will become a child of the
  // test_graphical_presetner.
  constexpr auto kGraphicalPresenterName = "test_graphical_presenter";
  constexpr auto kGraphicalPresenterUrl = "#meta/test_graphical_presenter.cm";
  realm.AddChild(kGraphicalPresenterName, kGraphicalPresenterUrl);
  realm
      .AddRoute(Route{.capabilities =
                          {
                              Protocol{fuchsia::logger::LogSink::Name_},
                              Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                              Protocol{fuchsia::sysmem::Allocator::Name_},
                              Protocol{fuchsia::tracing::provider::Registry::Name_},
                              Protocol{fuchsia::vulkan::loader::Loader::Name_},
                              Protocol{fuchsia::ui::composition::Flatland::Name_},
                              Protocol{fuchsia::ui::composition::Allocator::Name_},
                              Protocol{fuchsia::ui::input3::Keyboard::Name_},
                          },
                      .source = {ParentRef()},
                      .targets = {ChildRef{kGraphicalPresenterName}}})
      .AddRoute(Route{.capabilities =
                          {
                              Protocol{fuchsia::element::GraphicalPresenter::Name_},
                          },
                      .source = {ChildRef{kGraphicalPresenterName}},
                      .targets = {ChildRef{kGuestManagerName}}})
      .AddRoute(Route{.capabilities =
                          {
                              Protocol{fuchsia::ui::app::ViewProvider::Name_},
                          },
                      .source = {ChildRef{kGraphicalPresenterName}},
                      .targets = {ParentRef()}});
}

std::optional<ViewDescriptor> FindDisplayView(ui_testing::UITestManager& ui_test_manager) {
  auto presenter_koid = ui_test_manager.ClientViewRefKoid();
  if (!presenter_koid) {
    return {};
  }
  auto presenter = ui_test_manager.FindViewFromSnapshotByKoid(*presenter_koid);
  if (!presenter || !presenter->has_children() || presenter->children().empty()) {
    return {};
  }
  return ui_test_manager.FindViewFromSnapshotByKoid(presenter->children()[0]);
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

std::unique_ptr<sys::ServiceDirectory> EnclosedGuest::StartWithRealmBuilder(
    zx::time deadline, GuestLaunchInfo& guest_launch_info) {
  auto realm_builder = component_testing::RealmBuilder::Create();
  InstallInRealm(realm_builder.root(), guest_launch_info);
  realm_root_ =
      std::make_unique<component_testing::RealmRoot>(realm_builder.Build(loop_.dispatcher()));
  return std::make_unique<sys::ServiceDirectory>(realm_root_->CloneRoot());
}

std::unique_ptr<sys::ServiceDirectory> EnclosedGuest::StartWithUITestManager(
    zx::time deadline, GuestLaunchInfo& guest_launch_info) {
  using component_testing::Directory;
  using component_testing::Protocol;
  using component_testing::Storage;

  // UITestManager allows us to run these tests against a hermetic UI stack (ex: to test
  // interactions with Flatland, GraphicalPresenter, and Input).
  //
  // As structured, the virtualization components will be run in a sub-realm created by the
  // UITestRealm. Some of the below config fields will allow us to route capabilities through that
  // realm.
  ui_testing::UITestRealm::Config ui_config;
  ui_config.scene_owner = ui_testing::UITestRealm::SceneOwnerType::SCENE_MANAGER;
  ui_config.use_input = true;
  ui_config.use_flatland = true;

  // These are services that we need to expose from the UITestRealm.
  ui_config.exposed_client_services = {guest_launch_info.interface_name,
                                       fuchsia::virtualization::LinuxManager::Name_,
                                       fuchsia::ui::app::ViewProvider::Name_};

  // These are the services we need to consume from the UITestRealm.
  ui_config.ui_to_client_services = {
      fuchsia::ui::composition::Flatland::Name_,
      fuchsia::ui::composition::Allocator::Name_,
      fuchsia::ui::input3::Keyboard::Name_,
  };

  // These are the parent services (from our cml) that we need the UITestRealm to forward to use so
  // that they can be routed to the guest manager.
  ui_config.passthrough_capabilities = {
      Protocol{fuchsia::kernel::HypervisorResource::Name_},
      Protocol{fuchsia::kernel::VmexResource::Name_},
      Protocol{fuchsia::sysinfo::SysInfo::Name_},
      Directory{
          .name = kDevGpuDirectory, .rights = fuchsia::io::R_STAR_DIR, .path = "/dev/class/gpu"},
      Storage{.name = "data", .path = "/data"},
  };

  // Now create and install the virtualization components into a new sub-realm.
  ui_test_manager_ = std::make_unique<ui_testing::UITestManager>(std::move(ui_config));
  auto guest_realm = ui_test_manager_->AddSubrealm();
  InstallInRealm(guest_realm, guest_launch_info);
  InstallTestGraphicalPresenter(guest_realm);
  ui_test_manager_->BuildRealm();
  ui_test_manager_->InitializeScene();
  return ui_test_manager_->CloneExposedServicesDirectory();
}

zx_status_t EnclosedGuest::Start(zx::time deadline) {
  using component_testing::RealmBuilder;
  using component_testing::RealmRoot;

  GuestLaunchInfo guest_launch_info;
  if (auto status = BuildLaunchInfo(&guest_launch_info); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failure building GuestLaunchInfo";
    return status;
  }

  // Tests must be explicit about GPU support in the tests.
  //
  // If we need GPU support we will launch with UITestManager to provide a hermetic instance of UI
  // and input services. Otherwise we will launch directly using RealmBuilder. We make this
  // distinction because UITestManager depends on the availability of vulkan and we can avoid that
  // dependency for tests that don't need to test any interactions with the UI stack.
  FX_CHECK(guest_launch_info.config.has_virtio_gpu())
      << "virtio-gpu support must be explicitly declared.";
  std::unique_ptr<sys::ServiceDirectory> realm_services;
  if (guest_launch_info.config.virtio_gpu()) {
    realm_services = StartWithUITestManager(deadline, guest_launch_info);
  } else {
    realm_services = StartWithRealmBuilder(deadline, guest_launch_info);
  }

  return LaunchInRealm(std::move(realm_services), guest_launch_info, deadline);
}

void EnclosedGuest::InstallInRealm(component_testing::Realm& realm,
                                   GuestLaunchInfo& guest_launch_info) {
  using component_testing::ChildRef;
  using component_testing::Directory;
  using component_testing::ParentRef;
  using component_testing::Protocol;
  using component_testing::Route;
  using component_testing::Storage;

  constexpr auto kFakeNetstackComponentName = "fake_netstack";

  realm.AddChild(kGuestManagerName, guest_launch_info.url);
  realm.AddLocalChild(kFakeNetstackComponentName, &fake_netstack_);

  realm
      .AddRoute(Route{.capabilities =
                          {
                              Protocol{fuchsia::logger::LogSink::Name_},
                              Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                              Protocol{fuchsia::sysmem::Allocator::Name_},
                              Protocol{fuchsia::tracing::provider::Registry::Name_},
                              Protocol{fuchsia::vulkan::loader::Loader::Name_},
                              Protocol{fuchsia::ui::composition::Flatland::Name_},
                              Protocol{fuchsia::ui::composition::Allocator::Name_},
                              Protocol{fuchsia::ui::input3::Keyboard::Name_},
                          },
                      .source = {ParentRef()},
                      .targets = {ChildRef{kGuestManagerName}}})
      .AddRoute(Route{.capabilities =
                          {
                              Protocol{fuchsia::kernel::HypervisorResource::Name_},
                              Protocol{fuchsia::kernel::VmexResource::Name_},
                              Protocol{fuchsia::sysinfo::SysInfo::Name_},
                              Directory{.name = kDevGpuDirectory,
                                        .rights = fuchsia::io::R_STAR_DIR,
                                        .path = "/dev/class/gpu"},
                              Storage{.name = "data", .path = "/data"},
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
                              Protocol{fuchsia::virtualization::LinuxManager::Name_},
                              Protocol{guest_launch_info.interface_name},
                          },
                      .source = ChildRef{kGuestManagerName},
                      .targets = {ParentRef()}});
}

zx_status_t EnclosedGuest::LaunchInRealm(std::unique_ptr<sys::ServiceDirectory> services,
                                         GuestLaunchInfo& guest_launch_info, zx::time deadline) {
  realm_services_ = std::move(services);
  Logger::Get().Reset();
  PeriodicLogger logger;

  fuchsia::virtualization::GuestManager_LaunchGuest_Result res;
  guest_manager_ =
      realm_services_
          ->Connect<fuchsia::virtualization::GuestManager>(guest_launch_info.interface_name)
          .Unbind()
          .BindSync();

  // Get whether the vsock device will be installed for this guest. This is used later to validate
  // whether we expect GetHostVsockEndpoint to succeed.
  const bool vsock_enabled =
      !guest_launch_info.config.has_virtio_vsock() || guest_launch_info.config.virtio_vsock();

  auto status =
      guest_manager_->LaunchGuest(std::move(guest_launch_info.config), guest_.NewRequest(), &res);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failure launching guest " << guest_launch_info.url;
    return status;
  }
  guest_cid_ = fuchsia::virtualization::DEFAULT_GUEST_CID;

  if (vsock_enabled && GetHostVsockEndpoint(vsock_.NewRequest()).is_error()) {
    FX_LOGS(ERROR) << "Failed to get host vsock endpoint";
    return ZX_ERR_INTERNAL;
  }

  // Launch the guest.
  logger.Start("Launching guest", zx::sec(5));
  std::optional<zx_status_t> guest_error;
  guest_.set_error_handler([&guest_error](zx_status_t status) { guest_error = status; });

  // Connect to guest serial, and log it to the logger.
  logger.Start("Connecting to guest serial", zx::sec(10));
  std::optional<zx::socket> get_serial_result;

  guest_->GetSerial(
      [&get_serial_result](zx::socket socket) { get_serial_result = std::move(socket); });

  bool success = RunLoopUntil(
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
  serial_logger_.emplace(&Logger::Get(), std::move(get_serial_result.value()));

  // Connect to guest console.
  logger.Start("Connecting to guest console", zx::sec(10));
  std::optional<fuchsia::virtualization::Guest_GetConsole_Result> get_console_result;
  guest_->GetConsole(
      [&get_console_result](fuchsia::virtualization::Guest_GetConsole_Result result) {
        get_console_result = std::move(result);
      });
  success = RunLoopUntil(
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
    FX_LOGS(ERROR) << "Failed to open guest console"
                   << static_cast<int32_t>(get_console_result->err());
    return ZX_ERR_INTERNAL;
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

fitx::result<::fuchsia::virtualization::GuestError> EnclosedGuest::ConnectToBalloon(
    ::fidl::InterfaceRequest<::fuchsia::virtualization::BalloonController> controller) {
  zx_status_t status = ZX_ERR_TIMED_OUT;
  fuchsia::virtualization::GuestError error;
  guest_->GetBalloonController(
      std::move(controller),
      [&status, &error](fuchsia::virtualization::Guest_GetBalloonController_Result result) {
        if (result.is_response()) {
          status = ZX_OK;
        } else {
          status = ZX_ERR_INTERNAL;
          error = result.err();
        }
      });

  const bool success = RunLoopUntil([&status] { return status != ZX_ERR_TIMED_OUT; },
                                    zx::deadline_after(zx::sec(20)));
  if (!success) {
    FX_LOGS(ERROR) << "Timed out waiting to get balloon controller";
    return fitx::error(fuchsia::virtualization::GuestError::DEVICE_NOT_PRESENT);
  }

  if (status != ZX_OK) {
    return fitx::error(error);
  }
  return fitx::ok();
}

fitx::result<::fuchsia::virtualization::GuestError> EnclosedGuest::GetHostVsockEndpoint(
    ::fidl::InterfaceRequest<::fuchsia::virtualization::HostVsockEndpoint> endpoint) {
  zx_status_t status = ZX_ERR_TIMED_OUT;
  fuchsia::virtualization::GuestError error;
  guest_->GetHostVsockEndpoint(
      std::move(endpoint),
      [&status, &error](fuchsia::virtualization::Guest_GetHostVsockEndpoint_Result result) {
        if (result.is_response()) {
          status = ZX_OK;
        } else {
          status = ZX_ERR_INTERNAL;
          error = result.err();
        }
      });

  const bool success = RunLoopUntil([&status] { return status != ZX_ERR_TIMED_OUT; },
                                    zx::deadline_after(zx::sec(20)));
  if (!success) {
    FX_LOGS(ERROR) << "Timed out waiting to get host vsock endpoint";
    return fitx::error(fuchsia::virtualization::GuestError::DEVICE_NOT_PRESENT);
  }

  if (status != ZX_OK) {
    return fitx::error(error);
  }
  return fitx::ok();
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

bool EnclosedGuest::RunLoopUntil(fit::function<bool()> condition, zx::time deadline) {
  while (zx::clock::get_monotonic() < deadline) {
    // Check our condition.
    if (condition()) {
      return true;
    }

    // Wait until next polling interval.
    GetLoop()->Run(zx::deadline_after(kLoopConditionStep));
    GetLoop()->ResetQuit();
  }

  return condition();
}

zx_status_t ZirconEnclosedGuest::BuildLaunchInfo(GuestLaunchInfo* launch_info) {
  launch_info->url = kZirconGuestUrl;
  launch_info->interface_name = fuchsia::virtualization::ZirconGuestManager::Name_;
  // Disable netsvc to avoid spamming the net device with logs.
  launch_info->config.mutable_cmdline_add()->emplace_back("netsvc.disable=true");
  launch_info->config.set_virtio_gpu(true);
  return ZX_OK;
}

EnclosedGuest::DisplayInfo EnclosedGuest::WaitForDisplay() {
  // Wait for the display view to render.
  std::optional<ViewDescriptor> view_descriptor;
  RunLoopUntil(
      [this, &view_descriptor] {
        view_descriptor = FindDisplayView(*ui_test_manager_);
        return view_descriptor.has_value();
      },
      zx::deadline_after(zx::sec(20)));

  // Now wait for the view to get focus.
  auto koid = view_descriptor->view_ref_koid();
  RunLoopUntil([this, koid] { return ui_test_manager_->ViewIsFocused(koid); },
               zx::time::infinite());

  const auto& extent = view_descriptor->layout().extent;
  return DisplayInfo{
      .width = static_cast<uint32_t>(std::round(extent.max.x - extent.min.x)),
      .height = static_cast<uint32_t>(std::round(extent.max.y - extent.min.y)),
  };
}

namespace {
fitx::result<std::string> EnsureValidZirconPsOutput(std::string_view ps_output) {
  if (ps_output.find("virtual-console") == std::string::npos) {
    return fitx::error("'virtual-console' cannot be found in 'ps' output");
  }
  if (ps_output.find("system-updater") == std::string::npos) {
    return fitx::error("'system-updater' cannot be found in 'ps' output");
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

  FX_LOGS(ERROR) << "Failed to wait for processes: " << EnsureValidZirconPsOutput(ps).error_value();
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

zx_status_t DebianEnclosedGuest::BuildLaunchInfo(GuestLaunchInfo* launch_info) {
  launch_info->url = kDebianGuestUrl;
  launch_info->interface_name = fuchsia::virtualization::DebianGuestManager::Name_;
  // Enable kernel debugging serial output.
  for (std::string_view cmd : kLinuxKernelSerialDebugCmdline) {
    launch_info->config.mutable_cmdline_add()->emplace_back(cmd);
  }
  launch_info->config.set_virtio_gpu(true);
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

zx_status_t TerminaEnclosedGuest::BuildLaunchInfo(GuestLaunchInfo* launch_info) {
  launch_info->url = kTerminaGuestUrl;
  launch_info->interface_name = fuchsia::virtualization::TerminaGuestManager::Name_;
  launch_info->config.set_virtio_gpu(false);

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

void TerminaEnclosedGuest::InstallInRealm(component_testing::Realm& realm,
                                          GuestLaunchInfo& guest_launch_info) {
  EnclosedGuest::InstallInRealm(realm, guest_launch_info);

  using component_testing::ConfigValue;
  realm.InitMutableConfigFromPackage(kGuestManagerName);
  realm.SetConfigValue(kGuestManagerName, "stateful_partition_type", "file");
  realm.SetConfigValue(kGuestManagerName, "stateful_partition_size",
                       ConfigValue::Uint64(128 * 1024 * 1024));
  realm.SetConfigValue(kGuestManagerName, "start_container_runtime", ConfigValue::Bool(false));

  // These correspond to the additional block devices supplied in BuildLaunchInfo.
  realm.SetConfigValue(kGuestManagerName, "additional_read_only_mounts",
                       ConfigValue{std::vector<std::string>{
                           "/dev/vde",
                           "/tmp/vm_extras",
                           "ext2",

                           "/dev/vdf",
                           "/tmp/test_utils",
                           "romfs",

                           "/dev/vdg",
                           "/tmp/extras",
                           "romfs",
                       }});
}

zx_status_t TerminaEnclosedGuest::WaitForSystemReady(zx::time deadline) {
  // Connect to the LinuxManager to get status updates on VM.
  auto linux_manager = ConnectToService<fuchsia::virtualization::LinuxManager>();
  std::optional<std::string> failure;
  bool done = false;
  linux_manager.events().OnGuestInfoChanged = [this, &done, &failure](auto label, auto info) {
    if (info.container_status() == fuchsia::virtualization::ContainerStatus::FAILED) {
      failure = std::move(info.failure_reason());
    } else if (info.container_status() == target_status_) {
      done = true;
    }
  };

  {
    PeriodicLogger logger("Wait for termina", zx::sec(1));
    if (!RunLoopUntil([&done, &failure] { return failure.has_value() || done; }, deadline)) {
      return ZX_ERR_TIMED_OUT;
    }
  }
  if (failure) {
    FX_LOGS(ERROR) << "Failed to start Termina: " << *failure;
    return ZX_ERR_UNAVAILABLE;
  }

  // Connect to vshd.
  fuchsia::virtualization::HostVsockEndpointPtr endpoint;
  FX_CHECK(GetHostVsockEndpoint(endpoint.NewRequest()).is_ok());

  command_runner_ = std::make_unique<vsh::BlockingCommandRunner>(std::move(endpoint));

  return ZX_OK;
}

zx_status_t TerminaEnclosedGuest::Execute(const std::vector<std::string>& command,
                                          const std::unordered_map<std::string, std::string>& env,
                                          zx::time deadline, std::string* result,
                                          int32_t* return_code) {
  std::string command_string = JoinArgVector(command);
  Logger::Get().WriteLine(command_string);

  std::vector<std::string> argv = {"sh", "-c", std::move(command_string)};
  auto command_result = command_runner_->Execute({argv, env});
  if (command_result.is_error()) {
    return command_result.error();
  }
  if (result) {
    Logger::Get().WriteLine("stdout:");
    Logger::Get().WriteLine(command_result.value().out);
    *result = std::move(command_result.value().out);
    if (!command_result.value().err.empty()) {
      Logger::Get().WriteLine("stderr:");
      Logger::Get().WriteLine(command_result.value().err);
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

zx_status_t TerminaEnclosedGuest::ShutdownAndWait(zx::time deadline) { return ZX_OK; }

zx_status_t TerminaContainerEnclosedGuest::BuildLaunchInfo(GuestLaunchInfo* launch_info) {
  zx_status_t status = TerminaEnclosedGuest::BuildLaunchInfo(launch_info);
  if (status != ZX_OK) {
    return status;
  }
  // Limit the amount of guest memory while we're putting /data on memfs. Without limits here we can
  // see some OOMs on asan bots.
  //
  // TODO(108756): Remove this once we no longer put the data partition on memfs.
  launch_info->config.set_guest_memory(uint64_t{1} * 1024 * 1024 * 1024);
  return ZX_OK;
}

void TerminaContainerEnclosedGuest::InstallInRealm(component_testing::Realm& realm,
                                                   GuestLaunchInfo& guest_launch_info) {
  EnclosedGuest::InstallInRealm(realm, guest_launch_info);

  using component_testing::ConfigValue;
  realm.InitMutableConfigFromPackage(kGuestManagerName);
  realm.SetConfigValue(kGuestManagerName, "stateful_partition_type", "file");
  realm.SetConfigValue(kGuestManagerName, "stateful_partition_size",
                       ConfigValue::Uint64(2ull * 1024 * 1024 * 1024));

  // These correspond to the additional block devices supplied in BuildLaunchInfo.
  realm.SetConfigValue(kGuestManagerName, "additional_read_only_mounts",
                       ConfigValue{std::vector<std::string>{
                           "/dev/vde",
                           "/tmp/vm_extras",
                           "ext2",

                           "/dev/vdf",
                           "/tmp/test_utils",
                           "romfs",

                           "/dev/vdg",
                           "/tmp/extras",
                           "romfs",
                       }});

  // Start the container and bootstrap from a local image file instead of the internet.
  realm.SetConfigValue(kGuestManagerName, "start_container_runtime", ConfigValue::Bool(true));
  realm.SetConfigValue(kGuestManagerName, "container_rootfs_path", "/tmp/extras/rootfs.tar.xz");
  realm.SetConfigValue(kGuestManagerName, "container_metadata_path", "/tmp/extras/lxd.tar.xz");
}

zx_status_t TerminaContainerEnclosedGuest::Execute(
    const std::vector<std::string>& argv, const std::unordered_map<std::string, std::string>& env,
    zx::time deadline, std::string* result, int32_t* return_code) {
  // Run the command in the container using lxc-exec.
  //
  // This is an environment needed for lxc itself. The provided |env| will be passed to the binary
  // in the container as part of the lxc command but this allows lxc-exec to work properly.
  std::unordered_map<std::string, std::string> lxc_env;
  lxc_env["LXD_DIR"] = "/mnt/stateful/lxd";
  lxc_env["LXD_CONF"] = "/mnt/stateful/lxd_conf";
  lxc_env["LXD_UNPRIVILEGED_ONLY"] = "true";

  // Build the lxc-exec command:
  //
  //   lxc exec <container_name> --env=VAR=VALUE... -- argv...
  std::vector<std::string> lxc_args = {"lxc", "exec", "penguin"};
  for (const auto& var : env) {
    lxc_args.push_back(fxl::StringPrintf("--env=%s=%s", var.first.c_str(), var.second.c_str()));
  }
  lxc_args.push_back("--");
  lxc_args.insert(lxc_args.end(), argv.begin(), argv.end());

  // Now just exec the lxc-exec command over vsh.
  return TerminaEnclosedGuest::Execute(lxc_args, lxc_env, deadline, result, return_code);
}

zx_status_t TerminaContainerEnclosedGuest::WaitForSystemReady(zx::time deadline) {
  return TerminaEnclosedGuest::WaitForSystemReady(deadline);
}
