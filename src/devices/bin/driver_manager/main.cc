// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/boot/cpp/fidl.h>
#include <fuchsia/kernel/cpp/fidl.h>
#include <getopt.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/devmgr-launcher/processargs.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/zx/event.h>
#include <lib/zx/port.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>
#include <threads.h>
#include <zircon/device/vfs.h>
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
#include <memory>

#include <fbl/string_printf.h>

#include "component_lifecycle.h"
#include "coordinator.h"
#include "devfs.h"
#include "driver_host_loader_service.h"
#include "driver_runner.h"
#include "fdio.h"
#include "src/devices/bin/driver_manager/fake_driver_index.h"
#include "src/devices/lib/log/log.h"
#include "src/lib/storage/vfs/cpp/managed_vfs.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/remote_dir.h"
#include "src/lib/storage/vfs/cpp/vfs.h"
#include "src/lib/storage/vfs/cpp/vmo_file.h"
#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"
#include "system_instance.h"

namespace {

// These are helpers for getting sets of parameters over FIDL
struct DriverManagerParams {
  bool driver_host_asan;
  bool driver_host_strict_linking;
  bool enable_ephemeral;
  bool log_to_debuglog;
  bool require_system;
  bool suspend_timeout_fallback;
  bool verbose;
  std::vector<fbl::String> eager_fallback_drivers;
};

DriverManagerParams GetDriverManagerParams(fuchsia_boot::Arguments::SyncClient& client) {
  fuchsia_boot::wire::BoolPair bool_req[]{
      // TODO(bwb): remove this or figure out how to make it work
      {"devmgr.devhost.asan", false},
      {"devmgr.devhost.strict-linking", false},
      {"devmgr.enable-ephemeral", false},
      {"devmgr.log-to-debuglog", false},
      {"devmgr.require-system", false},
      // Turn it on by default. See fxbug.dev/34577
      {"devmgr.suspend-timeout-fallback", true},
      {"devmgr.verbose", false},
  };
  auto bool_resp = client.GetBools(fidl::unowned_vec(bool_req));
  if (!bool_resp.ok()) {
    return {};
  }

  auto drivers = client.GetString("devmgr.bind-eager");
  std::vector<fbl::String> eager_fallback_drivers;
  if (drivers.ok() && !drivers->value.is_null() && !drivers->value.empty()) {
    std::string list(drivers->value.data(), drivers->value.size());
    size_t pos;
    while ((pos = list.find(',')) != std::string::npos) {
      eager_fallback_drivers.emplace_back(list.substr(0, pos));
      list.erase(0, pos + 1);
    }
    eager_fallback_drivers.emplace_back(std::move(list));
  }
  return {
      .driver_host_asan = bool_resp->values[0],
      .driver_host_strict_linking = bool_resp->values[1],
      .enable_ephemeral = bool_resp->values[2],
      .log_to_debuglog = bool_resp->values[3],
      .require_system = bool_resp->values[4],
      .suspend_timeout_fallback = bool_resp->values[5],
      .verbose = bool_resp->values[6],
      .eager_fallback_drivers = std::move(eager_fallback_drivers),
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
  // be used.
  fbl::Vector<std::string> driver_search_paths;
  // Load the drivers with these paths.  The specified drivers do not need to
  // be in directories in |driver_search_paths|.
  fbl::Vector<const char*> load_drivers;
  // Connect the stdout and stderr file descriptors for this program to a
  // debuglog handle acquired with fuchsia.boot.WriteOnlyLog.
  bool log_to_debuglog = false;
  // Do not exit driver manager after suspending the system.
  bool no_exit_after_suspend = false;
  // Path prefix for binaries/drivers/libraries etc.
  std::string path_prefix = "/boot/";
  // Use this driver as the sys_device driver.  If nullptr, the default will
  // be used.
  std::string sys_device_driver;
  // Use the default loader rather than the one provided by fshost.
  bool use_default_loader = false;
  // Use the driver runner, which allows driver components to be loaded.
  bool use_driver_runner = false;
};

DriverManagerArgs ParseDriverManagerArgs(int argc, char** argv) {
  enum {
    kDriverSearchPath,
    kLoadDriver,
    kLogToDebuglog,
    kNoExitAfterSuspend,
    kPathPrefix,
    kSysDeviceDriver,
    kUseDefaultLoader,
    kUseDriverRunner,
  };
  option options[] = {
      {"driver-search-path", required_argument, nullptr, kDriverSearchPath},
      {"load-driver", required_argument, nullptr, kLoadDriver},
      {"log-to-debuglog", no_argument, nullptr, kLogToDebuglog},
      {"no-exit-after-suspend", no_argument, nullptr, kNoExitAfterSuspend},
      {"path-prefix", required_argument, nullptr, kPathPrefix},
      {"sys-device-driver", required_argument, nullptr, kSysDeviceDriver},
      {"use-default-loader", no_argument, nullptr, kUseDefaultLoader},
      {"use-driver-runner", no_argument, nullptr, kUseDriverRunner},
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
      case kDriverSearchPath:
        args.driver_search_paths.push_back(optarg);
        break;
      case kLoadDriver:
        args.load_drivers.push_back(optarg);
        break;
      case kLogToDebuglog:
        args.log_to_debuglog = true;
        break;
      case kNoExitAfterSuspend:
        args.no_exit_after_suspend = true;
        break;
      case kPathPrefix:
        args.path_prefix = std::string(optarg);
        break;
      case kSysDeviceDriver:
        check_not_duplicated(args.sys_device_driver);
        args.sys_device_driver = optarg;
        break;
      case kUseDefaultLoader:
        args.use_default_loader = true;
        break;
      case kUseDriverRunner:
        args.use_driver_runner = true;
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

  zx::channel local, remote;
  status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }
  auto path = fbl::StringPrintf("/svc/%s", fuchsia_boot::Arguments::Name);
  status = fdio_service_connect(path.data(), remote.release());
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to get boot arguments service handle: %s", zx_status_get_string(status));
    return status;
  }

  auto boot_args = fuchsia_boot::Arguments::SyncClient{std::move(local)};
  auto driver_manager_params = GetDriverManagerParams(boot_args);
  auto driver_manager_args = ParseDriverManagerArgs(argc, argv);

  if (driver_manager_params.verbose) {
    FX_LOG_SET_SEVERITY(ALL);
  }
  if (driver_manager_params.log_to_debuglog || driver_manager_args.log_to_debuglog) {
    status = log_to_debuglog();
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to reconfigure logger to use debuglog: %s", zx_status_get_string(status));
      return status;
    }
  }
  // Set up the default values for our arguments if they weren't given.
  if (driver_manager_args.driver_search_paths.size() == 0) {
    driver_manager_args.driver_search_paths.push_back(driver_manager_args.path_prefix + "driver");
  }
  if (driver_manager_args.sys_device_driver.empty()) {
    driver_manager_args.sys_device_driver =
        driver_manager_args.path_prefix + "driver/platform-bus.so";
  }

  SuspendCallback suspend_callback = [&driver_manager_args](zx_status_t status) {
    if (status != ZX_OK) {
      LOGF(ERROR, "Error suspending devices while stopping the component:%s",
           zx_status_get_string(status));
    }
    if (!driver_manager_args.no_exit_after_suspend) {
      LOGF(INFO, "Exiting driver manager gracefully");
      // TODO(fxb:52627) This event handler should teardown devices and driver hosts
      // properly for system state transitions where driver manager needs to go down.
      // Exiting like so, will not run all the destructors and clean things up properly.
      // Instead the main devcoordinator loop should be quit.
      exit(0);
    }
  };

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  svc::Outgoing outgoing(loop.dispatcher());
  InspectManager inspect_manager(loop.dispatcher());

  std::optional<DriverRunner> driver_runner;
  std::optional<FakeDriverIndex> driver_index;
  if (driver_manager_args.use_driver_runner) {
    const auto realm_path = fbl::StringPrintf("/svc/%s", fuchsia_sys2::Realm::Name);
    auto endpoints = fidl::CreateEndpoints<fuchsia_sys2::Realm>();
    if (endpoints.is_error()) {
      return endpoints.status_value();
    }
    status = fdio_service_connect(realm_path.data(), endpoints->server.TakeChannel().release());
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to connect to '%s': %s", realm_path.data(), zx_status_get_string(status));
      return status;
    }
    // TODO(fxbug.dev/33183): Replace this with a driver_index component.
    driver_index.emplace(loop.dispatcher(),
                         [](auto args) -> zx::status<FakeDriverIndex::MatchResult> {
                           std::string_view name(args.name().data(), args.name().size());
                           if (name == "board") {
                             return zx::ok(FakeDriverIndex::MatchResult{
                                 .url = "fuchsia-boot:///#meta/x64.cm",
                                 .matched_args = std::move(args),
                             });
                           } else {
                             return zx::error(ZX_ERR_NOT_FOUND);
                           }
                         });
    auto driver_index_client = driver_index->Connect();
    if (driver_index_client.is_error()) {
      LOGF(INFO, "Failed to conect to DriverIndex: %s",
           zx_status_get_string(driver_index_client.status_value()));
      return driver_index_client.status_value();
    }

    driver_runner.emplace(std::move(endpoints->client), std::move(driver_index_client.value()),
                          &inspect_manager.inspector(), loop.dispatcher());
    auto publish = driver_runner->PublishComponentRunner(outgoing.svc_dir());
    if (publish.is_error()) {
      return publish.error_value();
    }
    auto start = driver_runner->StartRootDriver("fuchsia-boot:///#meta/platform_bus2.cm");
    if (start.is_error()) {
      return start.error_value();
    }
  }

  CoordinatorConfig config;
  SystemInstance system_instance;
  config.boot_args = &boot_args;
  config.require_system = driver_manager_params.require_system;
  config.asan_drivers = driver_manager_params.driver_host_asan;
  config.suspend_fallback = driver_manager_params.suspend_timeout_fallback;
  config.log_to_debuglog =
      driver_manager_params.log_to_debuglog || driver_manager_args.log_to_debuglog;
  config.verbose = driver_manager_params.verbose;
  config.fs_provider = &system_instance;
  config.path_prefix = driver_manager_args.path_prefix;
  config.eager_fallback_drivers = std::move(driver_manager_params.eager_fallback_drivers);
  config.enable_ephemeral = driver_manager_params.enable_ephemeral;

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
  status = system_instance.CreateDriverHostJob(root_job, &config.driver_host_job);
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to create driver_host job: %s", zx_status_get_string(status));
    return status;
  }

  zx_handle_t oom_event;
  status = zx_system_get_event(root_job.get(), ZX_SYSTEM_EVENT_OUT_OF_MEMORY, &oom_event);
  if (status != ZX_OK) {
    LOGF(INFO, "Failed to get OOM event, assuming test environment and continuing");
  } else {
    config.oom_event = zx::event(oom_event);
  }

  Coordinator coordinator(std::move(config), &inspect_manager, loop.dispatcher());

  // Services offered to the rest of the system.
  status = coordinator.InitOutgoingServices(outgoing.svc_dir());
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to initialize outgoing services: %s", zx_status_get_string(status));
    return status;
  }

  // Check if whatever launched devcoordinator gave a channel to be connected to the
  // outgoing services directory. This is for use in tests to let the test environment see
  // outgoing services.
  zx::channel outgoing_svc_dir_client(
      zx_take_startup_handle(DEVMGR_LAUNCHER_OUTGOING_SERVICES_HND));
  if (outgoing_svc_dir_client.is_valid()) {
    status = outgoing.Serve(std::move(outgoing_svc_dir_client));
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to bind outgoing services: %s", zx_status_get_string(status));
      return status;
    }
  }

  status = coordinator.InitCoreDevices(driver_manager_args.sys_device_driver.c_str());
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to initialize core devices: %s", zx_status_get_string(status));
    return status;
  }

  devfs_init(coordinator.root_device(), loop.dispatcher());
  devfs_publish(coordinator.root_device(), coordinator.misc_device());
  devfs_publish(coordinator.root_device(), coordinator.sys_device());
  devfs_publish(coordinator.root_device(), coordinator.test_device());
  devfs_connect_diagnostics(fidl::UnownedClientEnd<fuchsia_io::Directory>(
      coordinator.inspect_manager().diagnostics_channel()));

  // Check if whatever launched devmgr gave a channel to be connected to /dev.
  // This is for use in tests to let the test environment see devfs.
  zx::channel devfs_client(zx_take_startup_handle(DEVMGR_LAUNCHER_DEVFS_ROOT_HND));
  if (devfs_client.is_valid()) {
    fdio_service_clone_to(devfs_root_borrow()->get(), devfs_client.release());
  }

  // Check if whatever launched devmgr gave a channel for component lifecycle events
  zx::channel component_lifecycle_request(zx_take_startup_handle(PA_LIFECYCLE));
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

  if (driver_manager_params.driver_host_strict_linking) {
    fbl::unique_fd lib_fd;
    status = fdio_open_fd("/pkg/lib",
                          ZX_FS_FLAG_DIRECTORY | ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_EXECUTABLE,
                          lib_fd.reset_and_get_address());
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to open /pkg/lib: %s", zx_status_get_string(status));
      return status;
    }

    auto loader_service = DriverHostLoaderService::Create(loop.dispatcher(), std::move(lib_fd));
    coordinator.set_loader_service_connector([ls = std::move(loader_service)](zx::channel* c) {
      auto conn = ls->Connect();
      if (conn.is_error()) {
        LOGF(ERROR, "Failed to add driver_host loader connection: %s", conn.status_string());
      } else {
        *c = conn->TakeChannel();
      }
      return conn.status_value();
    });
  } else if (!driver_manager_args.use_default_loader) {
    coordinator.set_loader_service_connector([&system_instance](zx::channel* c) {
      zx_status_t status = system_instance.clone_fshost_ldsvc(c);
      if (status != ZX_OK) {
        LOGF(ERROR, "Failed to clone fshost loader for driver_host: %s",
             zx_status_get_string(status));
      }
      return status;
    });
  }

  for (const std::string& path : driver_manager_args.driver_search_paths) {
    find_loadable_drivers(path, fit::bind_member(&coordinator, &Coordinator::DriverAddedInit));
  }
  for (const char* driver : driver_manager_args.load_drivers) {
    load_driver(driver, fit::bind_member(&coordinator, &Coordinator::DriverAddedInit));
  }

  coordinator.PrepareProxy(coordinator.sys_device(), nullptr);
  coordinator.PrepareProxy(coordinator.test_device(), nullptr);

  // Initial bind attempt for drivers enumerated at startup.
  coordinator.BindDrivers();
  if (coordinator.require_system()) {
    LOGF(INFO, "Full system required, fallback drivers will be loaded after '/system' is loaded");
  } else {
    coordinator.BindFallbackDrivers();
  }

  outgoing.root_dir()->AddEntry("dev",
                                fbl::MakeRefCounted<fs::RemoteDir>(system_instance.CloneFs("dev")));
  outgoing.root_dir()->AddEntry("diagnostics", fbl::MakeRefCounted<fs::RemoteDir>(
                                                   system_instance.CloneFs("dev/diagnostics")));
  outgoing.ServeFromStartupInfo();

  coordinator.set_running(true);
  status = loop.Run();
  LOGF(ERROR, "Coordinator exited unexpectedly: %s", zx_status_get_string(status));
  return status;
}
