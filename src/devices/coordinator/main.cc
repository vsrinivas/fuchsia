// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/boot/c/fidl.h>
#include <fuchsia/ldsvc/llcpp/fidl.h>
#include <getopt.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/boot-args/boot-args.h>
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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <fs/managed_vfs.h>
#include <fs/pseudo_dir.h>
#include <fs/remote_dir.h>
#include <fs/vfs.h>
#include <fs/vmo_file.h>

#include "coordinator.h"
#include "devfs.h"
#include "devhost-loader-service.h"
#include "fdio.h"
#include "log.h"
#include "system-instance.h"

namespace {

constexpr char kRootJobPath[] = "/svc/" fuchsia_boot_RootJob_Name;
constexpr char kRootResourcePath[] = "/svc/" fuchsia_boot_RootResource_Name;

// Get the root job from the root job service.
zx_status_t get_root_job(zx::job* root_job) {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }
  status = fdio_service_connect(kRootJobPath, remote.release());
  if (status != ZX_OK) {
    return status;
  }
  return fuchsia_boot_RootJobGet(local.get(), root_job->reset_and_get_address());
}

// Get the root resource from the root resource service. Not receiving the
// startup handle is logged, but not fatal.  In test environments, it would not
// be present.
zx_status_t get_root_resource(zx::resource* root_resource) {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }
  status = fdio_service_connect(kRootResourcePath, remote.release());
  if (status != ZX_OK) {
    return status;
  }
  return fuchsia_boot_RootResourceGet(local.get(), root_resource->reset_and_get_address());
}

void ParseArgs(int argc, char** argv, devmgr::DevmgrArgs* out) {
  enum {
    kDriverSearchPath,
    kLoadDriver,
    kSysDeviceDriver,
    kNoStartSvchost,
    kDisableBlockWatcher,
    kDisableNetsvc,
  };
  option options[] = {
      {"driver-search-path", required_argument, nullptr, kDriverSearchPath},
      {"load-driver", required_argument, nullptr, kLoadDriver},
      {"sys-device-driver", required_argument, nullptr, kSysDeviceDriver},
      {"no-start-svchost", no_argument, nullptr, kNoStartSvchost},
      {"disable-block-watcher", no_argument, nullptr, kDisableBlockWatcher},
      {"disable-netsvc", no_argument, nullptr, kDisableNetsvc},
  };

  auto print_usage_and_exit = [options]() {
    printf("devcoordinator: supported arguments:\n");
    for (const auto& option : options) {
      printf("  --%s\n", option.name);
    }
    abort();
  };

  auto check_not_duplicated = [print_usage_and_exit](const char* arg) {
    if (arg != nullptr) {
      printf("devcoordinator: duplicated argument\n");
      print_usage_and_exit();
    }
  };

  // Reset the args state
  *out = devmgr::DevmgrArgs();

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
      default:
        print_usage_and_exit();
    }
  }
}

zx_status_t CreateDevhostJob(const zx::job& root_job, zx::job* devhost_job_out) {
  zx::job devhost_job;
  zx_status_t status = zx::job::create(root_job, 0u, &devhost_job);
  if (status != ZX_OK) {
    log(ERROR, "devcoordinator: unable to create devhost job\n");
    return status;
  }
  static const zx_policy_basic_v2_t policy[] = {
      {ZX_POL_BAD_HANDLE, ZX_POL_ACTION_ALLOW_EXCEPTION, ZX_POL_OVERRIDE_DENY},
  };
  status = devhost_job.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_BASIC_V2, &policy,
                                  fbl::count_of(policy));
  if (status != ZX_OK) {
    log(ERROR, "devcoordinator: zx_job_set_policy() failed\n");
    return status;
  }
  status = devhost_job.set_property(ZX_PROP_NAME, "zircon-drivers", 15);
  if (status != ZX_OK) {
    log(ERROR, "devcoordinator: zx_job_set_property() failed\n");
    return status;
  }

  *devhost_job_out = std::move(devhost_job);
  return ZX_OK;
}

}  // namespace

int main(int argc, char** argv) {
  devmgr::BootArgs boot_args;
  zx_status_t status = devmgr::BootArgs::CreateFromArgumentsService(&boot_args);
  if (status != ZX_OK) {
    fprintf(stderr,
            "devcoordinator: failed to get boot arguments, assuming test "
            "environment and continuing\n");
  }

  if (boot_args.GetBool("devmgr.verbose", false)) {
    devmgr::log_flags |= LOG_ALL;
  }

  devmgr::DevmgrArgs devmgr_args;
  ParseArgs(argc, argv, &devmgr_args);
  // Set up the default values for our arguments if they weren't given.
  if (devmgr_args.driver_search_paths.size() == 0) {
    devmgr_args.driver_search_paths.push_back("/boot/driver");
  }
  if (devmgr_args.sys_device_driver == nullptr) {
    devmgr_args.sys_device_driver = "/boot/driver/platform-bus.so";
  }

  bool require_system = boot_args.GetBool("devmgr.require-system", false);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  devmgr::CoordinatorConfig config{};
  SystemInstance system_instance;
  config.dispatcher = loop.dispatcher();
  config.boot_args = &boot_args;
  config.require_system = require_system;
  // TODO(bwb): remove this or figure out how to make it work
  config.asan_drivers = boot_args.GetBool("devmgr.devhost.asan", false);
  // Turn it on by default. See fxb/34577
  config.suspend_fallback = boot_args.GetBool("devmgr.suspend-timeout-fallback", true);
  config.disable_netsvc = devmgr_args.disable_netsvc;
  config.fs_provider = &system_instance;

  // TODO(ZX-4178): Remove all uses of the root resource.
  status = get_root_resource(&config.root_resource);
  if (status != ZX_OK) {
    fprintf(stderr,
            "devcoordinator: failed to get root resource, assuming test "
            "environment and continuing\n");
  }
  // TODO(ZX-4177): Remove all uses of the root job.
  zx::job root_job;
  status = get_root_job(&root_job);
  if (status != ZX_OK) {
    fprintf(stderr, "devcoordinator: failed to get root job: %d\n", status);
    return 1;
  }
  status = CreateDevhostJob(root_job, &config.devhost_job);
  if (status != ZX_OK) {
    fprintf(stderr, "devcoordinator: failed to create devhost job: %d\n", status);
    return 1;
  }

  zx_handle_t lowmem_event;
  status = zx_system_get_event(root_job.get(), ZX_SYSTEM_EVENT_LOW_MEMORY, &lowmem_event);
  if (status != ZX_OK) {
    fprintf(stderr,
            "devcoordinator: failed to get lowmem event, assuming test "
            "environment and continuing\n");
  } else {
    config.lowmem_event = zx::event(lowmem_event);
  }

  devmgr::Coordinator coordinator(std::move(config));
  status = coordinator.InitializeCoreDevices(devmgr_args.sys_device_driver);
  if (status != ZX_OK) {
    log(ERROR, "devcoordinator: failed to initialize core devices\n");
    return 1;
  }

  devmgr::devfs_init(coordinator.root_device(), loop.dispatcher());
  devfs_publish(coordinator.root_device(), coordinator.misc_device());
  devfs_publish(coordinator.root_device(), coordinator.sys_device());
  devfs_publish(coordinator.root_device(), coordinator.test_device());

  // Check if whatever launched devmgr gave a channel to be connected to /dev.
  // This is for use in tests to let the test environment see devfs.
  zx::channel devfs_client(zx_take_startup_handle(DEVMGR_LAUNCHER_DEVFS_ROOT_HND));
  if (devfs_client.is_valid()) {
    fdio_service_clone_to(devmgr::devfs_root_borrow()->get(), devfs_client.release());
  }

  status = system_instance.CreateSvcJob(root_job);
  if (status != ZX_OK) {
    return 1;
  }

  status = system_instance.CreateFuchsiaJob(root_job);
  if (status != ZX_OK) {
    return 1;
  }

  zx::channel fshost_client, fshost_server;
  status = zx::channel::create(0, &fshost_client, &fshost_server);
  if (status != ZX_OK) {
    fprintf(stderr, "devcoordinator: failed to create fshost channels %s\n",
            zx_status_get_string(status));
    return 1;
  }
  status = system_instance.PrepareChannels();
  if (status != ZX_OK) {
    fprintf(stderr, "devcoordinator: failed to create other system channels %s\n",
            zx_status_get_string(status));
    return 1;
  }

  if (devmgr_args.start_svchost) {
    status = system_instance.StartSvchost(root_job, require_system, &coordinator,
                                          std::move(fshost_client));
    if (status != ZX_OK) {
      fprintf(stderr, "devcoordinator: failed to start svchost: %s\n",
              zx_status_get_string(status));
      return 1;
    }
  } else {
    status = system_instance.ReuseExistingSvchost();
    if (status != ZX_OK) {
      fprintf(stderr, "devcoordinator: failed to reuse existing svchost: %s\n",
              zx_status_get_string(status));
      return 1;
    }
  }

  // Check if whatever launched devcoordinator gave a channel to be connected to the
  // outgoing services directory. This is for use in tests to let the test environment see
  // outgoing services.
  zx::channel outgoing_svc_dir_client(
      zx_take_startup_handle(DEVMGR_LAUNCHER_OUTGOING_SERVICES_HND));
  if (outgoing_svc_dir_client.is_valid()) {
    status = coordinator.BindOutgoingServices(std::move(outgoing_svc_dir_client));
    if (status != ZX_OK) {
      fprintf(stderr, "devcoordinator: failed to bind outgoing services\n");
      return 1;
    }
  }

  system_instance.devmgr_vfs_init(&coordinator, devmgr_args, std::move(fshost_server));

  // If this is not a full Fuchsia build, do not setup appmgr services, as
  // this will delay startup.
  if (!require_system) {
    devmgr::devmgr_disable_appmgr_services();
  }

  thrd_t t;

  auto pwrbtn_starter_args = std::make_unique<SystemInstance::ServiceStarterArgs>();
  pwrbtn_starter_args->instance = &system_instance;
  pwrbtn_starter_args->coordinator = &coordinator;
  int ret = thrd_create_with_name(&t, SystemInstance::pwrbtn_monitor_starter,
                                  pwrbtn_starter_args.release(), "pwrbtn-monitor-starter");
  if (ret != thrd_success) {
    log(ERROR, "devcoordinator: failed to create pwrbtn monitor starter thread\n");
    return 1;
  }
  thrd_detach(t);

  system_instance.start_console_shell(boot_args);

  auto service_starter_args = std::make_unique<SystemInstance::ServiceStarterArgs>();
  service_starter_args->instance = &system_instance;
  service_starter_args->coordinator = &coordinator;
  ret = thrd_create_with_name(&t, SystemInstance::service_starter, service_starter_args.release(),
                              "service-starter");
  if (ret != thrd_success) {
    log(ERROR, "devcoordinator: failed to create service starter thread\n");
    return 1;
  }
  thrd_detach(t);

  std::unique_ptr<devmgr::DevhostLoaderService> loader_service;
  if (boot_args.GetBool("devmgr.devhost.strict-linking", false)) {
    status =
        devmgr::DevhostLoaderService::Create(loop.dispatcher(), &system_instance, &loader_service);
    if (status != ZX_OK) {
      return 1;
    }
    coordinator.set_loader_service_connector(
        [loader_service = std::move(loader_service)](zx::channel* c) {
          zx_status_t status = loader_service->Connect(c);
          if (status != ZX_OK) {
            log(ERROR, "devcoordinator: failed to add devhost loader connection: %s\n",
                zx_status_get_string(status));
          }
          return status;
        });
  } else {
    coordinator.set_loader_service_connector([&system_instance](zx::channel* c) {
      zx_status_t status = system_instance.clone_fshost_ldsvc(c);
      if (status != ZX_OK) {
        fprintf(stderr, "devcoordinator: failed to clone fshost loader for devhost: %s\n",
                zx_status_get_string(status));
      }
      return status;
    });
  }

  for (const char* path : devmgr_args.driver_search_paths) {
    devmgr::find_loadable_drivers(
        path, fit::bind_member(&coordinator, &devmgr::Coordinator::DriverAddedInit));
  }
  for (const char* driver : devmgr_args.load_drivers) {
    devmgr::load_driver(driver,
                        fit::bind_member(&coordinator, &devmgr::Coordinator::DriverAddedInit));
  }

  // Special case early handling for the ramdisk boot
  // path where /system is present before the coordinator
  // starts.  This avoids breaking the "priority hack" and
  // can be removed once the real driver priority system
  // exists.
  if (coordinator.system_available()) {
    status = coordinator.ScanSystemDrivers();
    if (status != ZX_OK) {
      return 1;
    }
  }

  if (coordinator.require_system() && !coordinator.system_loaded()) {
    printf(
        "devcoordinator: full system required, ignoring fallback drivers until /system is "
        "loaded\n");
  } else {
    coordinator.UseFallbackDrivers();
  }

  coordinator.PrepareProxy(coordinator.sys_device(), nullptr);
  coordinator.PrepareProxy(coordinator.test_device(), nullptr);
  // Initial bind attempt for drivers enumerated at startup.
  coordinator.BindDrivers();

  // Expose /dev directory for use in sysinfo service; specifically to connect to /dev/sys/platform
  auto outgoing_dir = fbl::AdoptRef<fs::PseudoDir>(new fs::PseudoDir());
  outgoing_dir->AddEntry(
      "dev", fbl::AdoptRef<fs::RemoteDir>(new fs::RemoteDir(system_instance.CloneFs("dev"))));

  fs::ManagedVfs outgoing_vfs = fs::ManagedVfs(loop.dispatcher());
  outgoing_vfs.ServeDirectory(outgoing_dir,
                              zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST)));

  coordinator.set_running(true);
  status = loop.Run();
  fprintf(stderr, "devcoordinator: coordinator exited unexpectedly: %d\n", status);
  return status == ZX_OK ? 0 : 1;
}
