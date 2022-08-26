// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.index/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fuchsia/boot/cpp/fidl.h>
#include <fuchsia/kernel/cpp/fidl.h>
#include <getopt.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/service/llcpp/service.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/zx/event.h>
#include <lib/zx/port.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>
#include <threads.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/policy.h>
#include <zircon/types.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>

#include <fbl/string_printf.h>

#include "component_lifecycle.h"
#include "coordinator.h"
#include "devfs.h"
#include "driver_host_loader_service.h"
#include "fdio.h"
#include "lib/async/cpp/task.h"
#include "src/devices/bin/driver_manager/devfs_exporter.h"
#include "src/devices/bin/driver_manager/device_watcher.h"
#include "src/devices/bin/driver_manager/v2/driver_development_service.h"
#include "src/devices/lib/log/log.h"
#include "src/lib/storage/vfs/cpp/managed_vfs.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/remote_dir.h"
#include "src/lib/storage/vfs/cpp/vfs.h"
#include "src/lib/storage/vfs/cpp/vmo_file.h"
#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"
#include "system_instance.h"
#include "v2/driver_runner.h"

namespace {

#define DEVMGR_LAUNCHER_DEVFS_ROOT_HND PA_HND(PA_USER1, 0)
#define DEVMGR_LAUNCHER_OUTGOING_SERVICES_HND PA_HND(PA_USER1, 1)

// These are helpers for getting sets of parameters over FIDL
struct DriverManagerParams {
  bool require_system;
  bool suspend_timeout_fallback;
  bool verbose;
  DriverHostCrashPolicy crash_policy;
  std::string root_driver;
  bool use_dfv2;
};

DriverManagerParams GetDriverManagerParams(fidl::WireSyncClient<fuchsia_boot::Arguments>& client) {
  fuchsia_boot::wire::BoolPair bool_req[]{
      {"devmgr.require-system", false},
      {"devmgr.suspend-timeout-fallback", true},
      {"devmgr.verbose", false},
      {"driver_manager.use_driver_framework_v2", false},
  };
  auto bool_resp =
      client->GetBools(fidl::VectorView<fuchsia_boot::wire::BoolPair>::FromExternal(bool_req));
  if (!bool_resp.ok()) {
    return {};
  }

  auto crash_policy = DriverHostCrashPolicy::kRestartDriverHost;
  auto response = client->GetString("driver-manager.driver-host-crash-policy");
  if (response.ok() && !response.value().value.is_null() && !response.value().value.empty()) {
    std::string crash_policy_str(response.value().value.get());
    if (crash_policy_str == "reboot-system") {
      crash_policy = DriverHostCrashPolicy::kRebootSystem;
    } else if (crash_policy_str == "restart-driver-host") {
      crash_policy = DriverHostCrashPolicy::kRestartDriverHost;
    } else if (crash_policy_str == "do-nothing") {
      crash_policy = DriverHostCrashPolicy::kDoNothing;
    } else {
      LOGF(ERROR, "Unexpected option for driver-manager.driver-host-crash-policy: %s",
           crash_policy_str.c_str());
    }
  }

  std::string root_driver = "";
  {
    auto response = client->GetString("driver_manager.root-driver");
    if (response.ok() && !response.value().value.is_null() && !response.value().value.empty()) {
      root_driver = std::string(response.value().value.data(), response.value().value.size());
    }
  }

  return {
      .require_system = bool_resp.value().values[0],
      .suspend_timeout_fallback = bool_resp.value().values[1],
      .verbose = bool_resp.value().values[2],
      .crash_policy = crash_policy,
      .root_driver = std::move(root_driver),
      .use_dfv2 = bool_resp.value().values[3],
  };
}

static const std::string kRootJobPath = "/svc/" + std::string(fuchsia::kernel::RootJob::Name_);
static const std::string kRootResourcePath =
    "/svc/" + std::string(fuchsia::boot::RootResource::Name_);

// Get the root job from the root job service.
zx_status_t get_root_job(zx::job* root_job) {
  fuchsia::kernel::RootJobSyncPtr root_job_ptr;
  zx_status_t status =
      fdio_service_connect(kRootJobPath.c_str(), root_job_ptr.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    return status;
  }
  return root_job_ptr->Get(root_job);
}

// Get the root resource from the root resource service. Not receiving the
// startup handle is logged, but not fatal.  In test environments, it would not
// be present.
zx_status_t get_root_resource(zx::resource* root_resource) {
  fuchsia::boot::RootResourceSyncPtr root_resource_ptr;
  zx_status_t status = fdio_service_connect(kRootResourcePath.c_str(),
                                            root_resource_ptr.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    return status;
  }
  return root_resource_ptr->Get(root_resource);
}

// Values parsed out of argv.  All paths described below are absolute paths.
struct DriverManagerArgs {
  // Load drivers from these directories.  If this is empty, the default will
  // be used (unless load_drivers is set).
  fbl::Vector<std::string> driver_search_paths;
  // Load the drivers with these paths. The specified drivers do not need to
  // be in directories in |driver_search_paths|.
  // If any of these drivers are set, then driver_search_paths default will not
  // be used.
  fbl::Vector<const char*> load_drivers;
  // Path prefix for binaries/drivers/libraries etc.
  std::string path_prefix = "/boot/";
  // Use this driver as the sys_device driver.  If nullptr, the default will
  // be used.
  std::string sys_device_driver;
  // If true then DriverManager uses DriverIndex for binding rather than
  // looking in /boot/drivers/. If this is false DriverManager will not
  // be able to load base packages.
  bool use_driver_index = false;
};

DriverManagerArgs ParseDriverManagerArgs(int argc, char** argv) {
  enum {
    kLoadDriver,
    kSysDeviceDriver,
    kUseDriverIndex,
  };
  option options[] = {
      {"load-driver", required_argument, nullptr, kLoadDriver},
      {"sys-device-driver", required_argument, nullptr, kSysDeviceDriver},
      {"use-driver-index", no_argument, nullptr, kUseDriverIndex},
      {0, 0, 0, 0},
  };

  auto print_usage_and_exit = [options]() {
    printf("driver_manager: supported arguments:\n");
    for (const auto& option : options) {
      printf("  --%s\n", option.name);
    }
    abort();
  };

  auto check_not_duplicated = [print_usage_and_exit](const std::string& arg) {
    if (!arg.empty()) {
      printf("driver_manager: duplicated argument\n");
      print_usage_and_exit();
    }
  };

  DriverManagerArgs args{};
  for (int opt; (opt = getopt_long(argc, argv, "", options, nullptr)) != -1;) {
    switch (opt) {
      case kLoadDriver:
        args.load_drivers.push_back(optarg);
        break;
      case kSysDeviceDriver:
        check_not_duplicated(args.sys_device_driver);
        args.sys_device_driver = optarg;
        break;
      case kUseDriverIndex:
        args.use_driver_index = true;
        break;
      default:
        print_usage_and_exit();
    }
  }
  return args;
}

}  // namespace

int main(int argc, char** argv) {
  zx_status_t status = StdoutToDebuglog::Init();
  if (status != ZX_OK) {
    LOGF(INFO, "Failed to redirect stdout to debuglog, assuming test environment and continuing");
  }

  auto args_result = service::Connect<fuchsia_boot::Arguments>();
  if (args_result.is_error()) {
    LOGF(ERROR, "Failed to get boot arguments service handle: %s", args_result.status_string());
    return args_result.error_value();
  }

  auto boot_args = fidl::WireSyncClient<fuchsia_boot::Arguments>{std::move(*args_result)};
  auto driver_manager_params = GetDriverManagerParams(boot_args);
  auto driver_manager_args = ParseDriverManagerArgs(argc, argv);

  if (driver_manager_params.verbose) {
    fx_logger_t* logger = fx_log_get_logger();
    if (logger) {
      fx_logger_set_min_severity(logger, std::numeric_limits<fx_log_severity_t>::min());
    }
  }
  if (!driver_manager_params.root_driver.empty()) {
    driver_manager_args.sys_device_driver = driver_manager_params.root_driver;
  }
  // Set up the default values for our arguments if they weren't given.
  if (driver_manager_args.driver_search_paths.size() == 0 &&
      driver_manager_args.load_drivers.size() == 0 && !driver_manager_args.use_driver_index) {
    driver_manager_args.driver_search_paths.push_back(driver_manager_args.path_prefix + "driver");
  }
  if (driver_manager_args.sys_device_driver.empty()) {
    driver_manager_args.sys_device_driver =
        driver_manager_args.path_prefix + "driver/platform-bus.so";
  }

  SuspendCallback suspend_callback = [](zx_status_t status) {
    if (status != ZX_OK) {
      // TODO(https://fxbug.dev/56208): Change this log back to error once isolated devmgr is fixed.
      LOGF(WARNING, "Error suspending devices while stopping the component:%s",
           zx_status_get_string(status));
    }
    LOGF(INFO, "Exiting driver manager gracefully");
    // TODO(fxb:52627) This event handler should teardown devices and driver hosts
    // properly for system state transitions where driver manager needs to go down.
    // Exiting like so, will not run all the destructors and clean things up properly.
    // Instead the main devcoordinator loop should be quit.
    exit(0);
  };

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  auto outgoing = component::OutgoingDirectory::Create(loop.dispatcher());
  InspectManager inspect_manager(loop.dispatcher());

  CoordinatorConfig config;
  SystemInstance system_instance;
  config.boot_args = &boot_args;
  config.require_system = driver_manager_params.require_system;
  config.verbose = driver_manager_params.verbose;
  config.fs_provider = &system_instance;
  config.path_prefix = driver_manager_args.path_prefix;
  config.crash_policy = driver_manager_params.crash_policy;

  // Waiting an infinite amount of time before falling back is effectively not
  // falling back at all.
  if (!driver_manager_params.suspend_timeout_fallback) {
    config.suspend_timeout = zx::duration::infinite();
  }

  if (driver_manager_args.use_driver_index) {
    auto driver_index_client = service::Connect<fuchsia_driver_index::DriverIndex>();
    if (driver_index_client.is_error()) {
      LOGF(ERROR, "Failed to connect to driver_index: %d", driver_index_client.error_value());
      return driver_index_client.error_value();
    }
    config.driver_index = fidl::WireSharedClient<fuchsia_driver_index::DriverIndex>(
        std::move(driver_index_client.value()), loop.dispatcher());
  }

  // TODO(fxbug.dev/33958): Remove all uses of the root resource.
  status = get_root_resource(&config.root_resource);
  if (status != ZX_OK) {
    LOGF(INFO, "Failed to get root resource, assuming test environment and continuing");
  }
  // TODO(fxbug.dev/33957): Remove all uses of the root job.
  zx::job root_job;
  status = get_root_job(&root_job);
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to get root job: %s", zx_status_get_string(status));
    return status;
  }

  zx_handle_t oom_event;
  status = zx_system_get_event(root_job.get(), ZX_SYSTEM_EVENT_OUT_OF_MEMORY, &oom_event);
  if (status != ZX_OK) {
    LOGF(INFO, "Failed to get OOM event, assuming test environment and continuing");
  } else {
    config.oom_event = zx::event(oom_event);
  }

  async::Loop firmware_loop(&kAsyncLoopConfigNeverAttachToThread);
  firmware_loop.StartThread("firmware-loop");

  Coordinator coordinator(std::move(config), &inspect_manager, loop.dispatcher(),
                          firmware_loop.dispatcher());

  // Services offered to the rest of the system.
  coordinator.InitOutgoingServices(outgoing);

  // Check if whatever launched devcoordinator gave a channel to be connected to the
  // outgoing services directory. This is for use in tests to let the test environment see
  // outgoing services.
  fidl::ServerEnd<fuchsia_io::Directory> outgoing_svc_dir_client(
      zx::channel(zx_take_startup_handle(DEVMGR_LAUNCHER_OUTGOING_SERVICES_HND)));
  if (outgoing_svc_dir_client.is_valid()) {
    auto result = outgoing.Serve(std::move(outgoing_svc_dir_client));
    if (result.is_error()) {
      LOGF(ERROR, "Failed to bind outgoing services: %s", result.status_string());
      return result.status_value();
    }
  }

  devfs_init(coordinator.root_device(), loop.dispatcher());

  std::optional<dfv2::DriverRunner> driver_runner;
  std::optional<driver_manager::DriverDevelopmentService> driver_development_service;
  std::optional<driver_manager::DevfsExporter> devfs_exporter;

  // Find and load v1 or v2 Drivers.
  if (!driver_manager_params.use_dfv2) {
    coordinator.PublishDriverDevelopmentService(outgoing);

    // V1 Drivers.
    status = system_instance.CreateDriverHostJob(root_job, &config.driver_host_job);
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to create driver_host job: %s", zx_status_get_string(status));
      return status;
    }

    coordinator.LoadV1Drivers(driver_manager_args.sys_device_driver.c_str(),
                              driver_manager_args.driver_search_paths,
                              driver_manager_args.load_drivers);
  } else {
    // V2 Drivers.
    LOGF(INFO, "Starting DriverRunner with root driver URL: %s",
         driver_manager_args.sys_device_driver.data());

    auto realm_result = service::Connect<fuchsia_component::Realm>();
    if (realm_result.is_error()) {
      return realm_result.error_value();
    }

    auto driver_index_result = service::Connect<fuchsia_driver_index::DriverIndex>();
    if (driver_index_result.is_error()) {
      LOGF(ERROR, "Failed to connect to driver_index: %d", driver_index_result.error_value());
      return driver_index_result.error_value();
    }

    driver_runner.emplace(std::move(realm_result.value()), std::move(driver_index_result.value()),
                          inspect_manager.inspector(), loop.dispatcher());
    driver_runner->PublishComponentRunner(outgoing);
    auto start = driver_runner->StartRootDriver(driver_manager_args.sys_device_driver);
    if (start.is_error()) {
      return start.error_value();
    }
    driver_development_service.emplace(driver_runner.value(), loop.dispatcher());
    driver_development_service->Publish(outgoing);

    devfs_exporter.emplace(coordinator.root_device()->devnode(), loop.dispatcher());
    devfs_exporter->PublishExporter(outgoing);

    driver_runner->ScheduleBaseDriversBinding();
  }

  devfs_connect_diagnostics(coordinator.inspect_manager().diagnostics_client());

  // Check if whatever launched devmgr gave a channel to be connected to /dev.
  // This is for use in tests to let the test environment see devfs.
  zx::channel devfs_client(zx_take_startup_handle(DEVMGR_LAUNCHER_DEVFS_ROOT_HND));
  if (devfs_client.is_valid()) {
    fdio_service_clone_to(devfs_root_borrow()->get(), devfs_client.release());
  }

  // Check if whatever launched devmgr gave a channel for component lifecycle events
  fidl::ServerEnd<fuchsia_process_lifecycle::Lifecycle> component_lifecycle_request(
      zx::channel(zx_take_startup_handle(PA_LIFECYCLE)));
  if (component_lifecycle_request.is_valid()) {
    status = devmgr::ComponentLifecycleServer::Create(loop.dispatcher(), &coordinator,
                                                      std::move(component_lifecycle_request),
                                                      std::move(suspend_callback));
    if (status != ZX_OK) {
      LOGF(ERROR, "driver_manager: Cannot create componentlifecycleserver: %s",
           zx_status_get_string(status));
      return status;
    }
  } else {
    LOGF(INFO,
         "No valid handle found for lifecycle events, assuming test environment and "
         "continuing");
  }

  system_instance.InstallDevFsIntoNamespace();
  system_instance.ServiceStarter(&coordinator);

  fbl::unique_fd lib_fd;
  {
    std::string library_path = driver_manager_args.path_prefix + "lib";
    status = fdio_open_fd(library_path.c_str(),
                          static_cast<uint32_t>(fio::wire::OpenFlags::kDirectory |
                                                fio::wire::OpenFlags::kRightReadable |
                                                fio::wire::OpenFlags::kRightExecutable),
                          lib_fd.reset_and_get_address());
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to open %s: %s", library_path.c_str(), zx_status_get_string(status));
      return status;
    }
  }

  // The loader needs its own thread because DriverManager makes synchronous calls to the
  // DriverHosts, which make synchronous calls to load their shared libraries.
  async::Loop loader_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto loader_service =
      DriverHostLoaderService::Create(loader_loop.dispatcher(), std::move(lib_fd));
  coordinator.set_loader_service_connector([ls = std::move(loader_service)](zx::channel* c) {
    auto conn = ls->Connect();
    if (conn.is_error()) {
      LOGF(ERROR, "Failed to add driver_host loader connection: %s", conn.status_string());
    } else {
      *c = conn->TakeChannel();
    }
    return conn.status_value();
  });
  loader_loop.StartThread();

  // TODO(https://fxbug.dev/99076) Remove this when this issue is fixed.
  LOGF(INFO, "driver_manager loader loop started");

  auto result = outgoing.AddProtocol<fuchsia_device_manager::DeviceWatcher>(
      [&loader_loop](fidl::ServerEnd<fuchsia_device_manager::DeviceWatcher> request) {
        auto watcher =
            std::make_unique<DeviceWatcher>("/dev/class/pciroot", loader_loop.dispatcher());
        fidl::BindServer(loader_loop.dispatcher(), std::move(request), std::move(watcher));
      },
      "fuchsia.hardware.pci.DeviceWatcher");
  ZX_ASSERT(result.is_ok());
  result = outgoing.AddProtocol<fuchsia_device_manager::DeviceWatcher>(
      [&loader_loop](fidl::ServerEnd<fuchsia_device_manager::DeviceWatcher> request) {
        auto watcher =
            std::make_unique<DeviceWatcher>("/dev/class/usb-device", loader_loop.dispatcher());
        fidl::BindServer(loader_loop.dispatcher(), std::move(request), std::move(watcher));
      },
      "fuchsia.hardware.usb.DeviceWatcher");
  ZX_ASSERT(result.is_ok());

  result = outgoing.AddDirectory(system_instance.CloneFs("dev"), "dev");
  ZX_ASSERT(result.is_ok());
  result = outgoing.AddDirectory(system_instance.CloneFs("dev/diagnostics"), "diagnostics");
  ZX_ASSERT(result.is_ok());
  // TODO(fxbug.dev/86318): Check this result once isolated devmgr is deprecated.
  (void)outgoing.ServeFromStartupInfo();

  // TODO(https://fxbug.dev/99076) Remove this when this issue is fixed.
  auto log_loop_start = std::make_unique<async::TaskClosure>(
      [] { LOGF(INFO, "driver_manager main loop is running"); });
  log_loop_start->Post(loop.dispatcher());

  coordinator.set_running(true);
  status = loop.Run();
  LOGF(ERROR, "Coordinator exited unexpectedly: %s", zx_status_get_string(status));
  return status;
}
