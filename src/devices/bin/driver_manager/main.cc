// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/boot/cpp/fidl.h>
#include <getopt.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/devmgr-launcher/processargs.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/fit/optional.h>
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
#include <fs/managed_vfs.h>
#include <fs/pseudo_dir.h>
#include <fs/remote_dir.h>
#include <fs/vfs.h>
#include <fs/vmo_file.h>

#include "component_lifecycle.h"
#include "coordinator.h"
#include "devfs.h"
#include "driver_host_loader_service.h"
#include "fdio.h"
#include "src/devices/lib/log/log.h"
#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"
#include "system_instance.h"

namespace {

// These are helpers for getting sets of parameters over FIDL
struct DriverManagerParams {
  bool driver_host_asan;
  bool driver_host_strict_linking;
  bool log_to_debuglog;
  bool require_system;
  bool suspend_timeout_fallback;
  bool verbose;
};

DriverManagerParams GetDriverManagerParams(llcpp::fuchsia::boot::Arguments::SyncClient& client) {
  llcpp::fuchsia::boot::BoolPair bool_req[]{
      // TODO(bwb): remove this or figure out how to make it work
      {"devmgr.devhost.asan", false},
      {"devmgr.devhost.strict-linking", false},
      {"devmgr.log-to-debuglog", false},
      {"devmgr.require-system", false},
      // Turn it on by default. See fxb/34577
      {"devmgr.suspend-timeout-fallback", true},
      {"devmgr.verbose", false},
  };
  auto bool_resp = client.GetBools(fidl::unowned_vec(bool_req));
  if (!bool_resp.ok()) {
    return {};
  }
  return {bool_resp->values[0], bool_resp->values[1], bool_resp->values[2],
          bool_resp->values[3], bool_resp->values[4], bool_resp->values[5]};
}

static const std::string kRootJobPath = "/svc/" + std::string(fuchsia::boot::RootJob::Name_);
static const std::string kRootResourcePath =
    "/svc/" + std::string(fuchsia::boot::RootResource::Name_);

// Get the root job from the root job service.
zx_status_t get_root_job(zx::job* root_job) {
  fuchsia::boot::RootJobSyncPtr root_job_ptr;
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

void ParseArgs(int argc, char** argv, DevmgrArgs* out) {
  enum {
    kDriverSearchPath,
    kLoadDriver,
    kSysDeviceDriver,
    kNoStartSvchost,
    kDisableBlockWatcher,
    kDisableNetsvc,
    kLogToDebuglog,
    kPathPrefix,
    kUseDefaultLoader,
  };
  option options[] = {
      {"driver-search-path", required_argument, nullptr, kDriverSearchPath},
      {"load-driver", required_argument, nullptr, kLoadDriver},
      {"sys-device-driver", required_argument, nullptr, kSysDeviceDriver},
      {"no-start-svchost", no_argument, nullptr, kNoStartSvchost},
      {"disable-block-watcher", no_argument, nullptr, kDisableBlockWatcher},
      {"disable-netsvc", no_argument, nullptr, kDisableNetsvc},
      {"log-to-debuglog", no_argument, nullptr, kLogToDebuglog},
      {"path-prefix", required_argument, nullptr, kPathPrefix},
      {"use-default-loader", no_argument, nullptr, kUseDefaultLoader},
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

  // Reset the args state
  *out = DevmgrArgs();

  int opt;
  while ((opt = getopt_long(argc, argv, "", options, nullptr)) != -1) {
    switch (opt) {
      case kDriverSearchPath:
        out->driver_search_paths.push_back(optarg);
        break;
      case kLoadDriver:
        out->load_drivers.push_back(optarg);
        break;
      case kSysDeviceDriver:
        check_not_duplicated(out->sys_device_driver);
        out->sys_device_driver = optarg;
        break;
      case kNoStartSvchost:
        out->start_svchost = false;
        break;
      case kDisableBlockWatcher:
        out->disable_block_watcher = true;
        break;
      case kDisableNetsvc:
        out->disable_netsvc = true;
        break;
      case kLogToDebuglog:
        out->log_to_debuglog = true;
        break;
      case kPathPrefix:
        out->path_prefix = std::string(optarg);
        break;
      case kUseDefaultLoader:
        out->use_default_loader = true;
        break;
      default:
        print_usage_and_exit();
    }
  }
}

zx_status_t CreateDriverHostJob(const zx::job& root_job, zx::job* driver_host_job_out) {
  zx::job driver_host_job;
  zx_status_t status = zx::job::create(root_job, 0u, &driver_host_job);
  if (status != ZX_OK) {
    LOGF(ERROR, "Unable to create driver_host job: %s", zx_status_get_string(status));
    return status;
  }
  static const zx_policy_basic_v2_t policy[] = {
      {ZX_POL_BAD_HANDLE, ZX_POL_ACTION_ALLOW_EXCEPTION, ZX_POL_OVERRIDE_DENY},
  };
  status = driver_host_job.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_BASIC_V2, &policy,
                                      fbl::count_of(policy));
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to set driver_host job policy: %s", zx_status_get_string(status));
    return status;
  }
  status = driver_host_job.set_property(ZX_PROP_NAME, "zircon-drivers", 15);
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to set driver_host job property: %s", zx_status_get_string(status));
    return status;
  }

  *driver_host_job_out = std::move(driver_host_job);
  return ZX_OK;
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
  auto path = fbl::StringPrintf("/svc/%s", llcpp::fuchsia::boot::Arguments::Name);
  status = fdio_service_connect(path.data(), remote.release());
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to get boot arguments service handle: %s", zx_status_get_string(status));
    return status;
  }

  auto boot_args = llcpp::fuchsia::boot::Arguments::SyncClient{std::move(local)};
  auto driver_manager_params = GetDriverManagerParams(boot_args);
  DevmgrArgs devmgr_args;
  ParseArgs(argc, argv, &devmgr_args);

  if (driver_manager_params.verbose) {
    FX_LOG_SET_SEVERITY(ALL);
  }
  if (driver_manager_params.log_to_debuglog || devmgr_args.log_to_debuglog) {
    zx_status_t status = log_to_debuglog();
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to reconfigure logger to use debuglog: %s", zx_status_get_string(status));
      return status;
    }
  }
  // Set up the default values for our arguments if they weren't given.
  if (devmgr_args.driver_search_paths.size() == 0) {
    devmgr_args.driver_search_paths.push_back("/boot/driver");
  }
  if (devmgr_args.sys_device_driver.empty()) {
    devmgr_args.sys_device_driver = devmgr_args.path_prefix + "driver/platform-bus.so";
  }

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  CoordinatorConfig config;
  SystemInstance system_instance;
  config.dispatcher = loop.dispatcher();
  config.boot_args = &boot_args;
  config.require_system = driver_manager_params.require_system;
  config.asan_drivers = driver_manager_params.driver_host_asan;
  config.suspend_fallback = driver_manager_params.suspend_timeout_fallback;
  config.log_to_debuglog = driver_manager_params.log_to_debuglog || devmgr_args.log_to_debuglog;
  config.verbose = driver_manager_params.verbose;
  config.disable_netsvc = devmgr_args.disable_netsvc;
  config.fs_provider = &system_instance;
  config.path_prefix = devmgr_args.path_prefix;

  // TODO(ZX-4178): Remove all uses of the root resource.
  status = get_root_resource(&config.root_resource);
  if (status != ZX_OK) {
    LOGF(INFO, "Failed to get root resource, assuming test environment and continuing");
  }
  // TODO(ZX-4177): Remove all uses of the root job.
  zx::job root_job;
  status = get_root_job(&root_job);
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to get root job: %s", zx_status_get_string(status));
    return status;
  }
  status = CreateDriverHostJob(root_job, &config.driver_host_job);
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

  Coordinator coordinator(std::move(config));

  // Services offered to the rest of the system.
  svc::Outgoing outgoing{loop.dispatcher()};
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

  status = coordinator.InitCoreDevices(devmgr_args.sys_device_driver.c_str());
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to initialize core devices: %s", zx_status_get_string(status));
    return status;
  }

  devfs_init(coordinator.root_device(), loop.dispatcher());
  devfs_publish(coordinator.root_device(), coordinator.misc_device());
  devfs_publish(coordinator.root_device(), coordinator.sys_device());
  devfs_publish(coordinator.root_device(), coordinator.test_device());
  devfs_connect_diagnostics(coordinator.inspect_manager().diagnostics_channel());

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
                                                      std::move(component_lifecycle_request));
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

  status = system_instance.CreateSvcJob(root_job);
  if (status != ZX_OK) {
    return status;
  }

  status = system_instance.PrepareChannels();
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to create other system channels: %s", zx_status_get_string(status));
    return status;
  }

  if (devmgr_args.start_svchost) {
    zx::channel root_server, root_client;
    status = zx::channel::create(0, &root_server, &root_client);
    if (status != ZX_OK) {
      return status;
    }
    status = outgoing.Serve(std::move(root_server));
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to bind outgoing services: %s", zx_status_get_string(status));
      return status;
    }
    status = system_instance.StartSvchost(root_job, root_client,
                                          driver_manager_params.require_system, &coordinator);
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to start svchost: %s", zx_status_get_string(status));
      return status;
    }
  } else {
    status = system_instance.ReuseExistingSvchost();
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to reuse existing svchost: %s", zx_status_get_string(status));
      return status;
    }
  }

  system_instance.devmgr_vfs_init();

  system_instance.start_console_shell(boot_args);

  thrd_t t;

  auto service_starter_args = std::make_unique<SystemInstance::ServiceStarterArgs>();
  service_starter_args->instance = &system_instance;
  service_starter_args->coordinator = &coordinator;
  int ret = thrd_create_with_name(&t, SystemInstance::service_starter,
                                  service_starter_args.release(), "service-starter");
  if (ret != thrd_success) {
    LOGF(ERROR, "Failed to create service starter thread: %d", ret);
    return ret;
  }
  thrd_detach(t);

  if (driver_manager_params.driver_host_strict_linking) {
    std::unique_ptr<DriverHostLoaderService> loader_service;
    status = DriverHostLoaderService::Create(loop.dispatcher(), &system_instance, &loader_service);
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to create loader service: %s", zx_status_get_string(status));
      return status;
    }
    coordinator.set_loader_service_connector([ls = std::move(loader_service)](zx::channel* c) {
      zx_status_t status = ls->Connect(c);
      if (status != ZX_OK) {
        LOGF(ERROR, "Failed to add driver_host loader connection: %s",
             zx_status_get_string(status));
      }
      return status;
    });
  } else if (!devmgr_args.use_default_loader) {
    coordinator.set_loader_service_connector([&system_instance](zx::channel* c) {
      zx_status_t status = system_instance.clone_fshost_ldsvc(c);
      if (status != ZX_OK) {
        LOGF(ERROR, "Failed to clone fshost loader for driver_host: %s",
             zx_status_get_string(status));
      }
      return status;
    });
  }

  for (const std::string& path : devmgr_args.driver_search_paths) {
    find_loadable_drivers(path, fit::bind_member(&coordinator, &Coordinator::DriverAddedInit));
  }
  for (const char* driver : devmgr_args.load_drivers) {
    load_driver(driver, fit::bind_member(&coordinator, &Coordinator::DriverAddedInit));
  }

  if (coordinator.require_system() && !coordinator.system_loaded()) {
    LOGF(INFO, "Full system required, ignoring fallback drivers until '/system' is loaded");
  } else {
    coordinator.UseFallbackDrivers();
  }

  coordinator.PrepareProxy(coordinator.sys_device(), nullptr);
  coordinator.PrepareProxy(coordinator.test_device(), nullptr);
  // Initial bind attempt for drivers enumerated at startup.
  coordinator.BindDrivers();

  // Expose /dev directory for use in sysinfo service; specifically to connect to /dev/sys/platform
  auto outgoing_dir = fbl::MakeRefCounted<fs::PseudoDir>();
  outgoing_dir->AddEntry("dev", fbl::MakeRefCounted<fs::RemoteDir>(system_instance.CloneFs("dev")));
  outgoing_dir->AddEntry("svc", fbl::MakeRefCounted<fs::RemoteDir>(system_instance.CloneFs("svc")));

  fs::ManagedVfs outgoing_vfs = fs::ManagedVfs(loop.dispatcher());
  outgoing_vfs.ServeDirectory(outgoing_dir,
                              zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST)));

  coordinator.set_running(true);
  status = loop.Run();
  LOGF(ERROR, "Coordinator exited unexpectedly: %s", zx_status_get_string(status));
  return status;
}
