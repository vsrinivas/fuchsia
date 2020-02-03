// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system-instance.h"

#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/hardware/virtioconsole/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>
#include <lib/zircon-internal/paths.h>
#include <lib/zx/debuglog.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/boot/image.h>
#include <zircon/status.h>
#include <zircon/syscalls/log.h>
#include <zircon/syscalls/policy.h>

#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>

#include "devfs.h"
#include "fdio.h"
#include "log.h"

struct ConsoleStarterArgs {
  SystemInstance* instance;
  devmgr::BootArgs* boot_args;
};

// Wait for the requested file.  Its parent directory must exist.
zx_status_t wait_for_file(const char* path, zx::time deadline) {
  char path_copy[PATH_MAX];
  if (strlen(path) >= PATH_MAX) {
    return ZX_ERR_INVALID_ARGS;
  }
  strcpy(path_copy, path);

  char* last_slash = strrchr(path_copy, '/');
  // Waiting on the root of the fs or paths with no slashes is not supported by this function
  if (last_slash == path_copy || last_slash == nullptr) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  last_slash[0] = 0;
  char* dirname = path_copy;
  char* basename = last_slash + 1;

  auto watch_func = [](int dirfd, int event, const char* fn, void* cookie) -> zx_status_t {
    auto basename = static_cast<const char*>(cookie);
    if (event != WATCH_EVENT_ADD_FILE) {
      return ZX_OK;
    }
    if (!strcmp(fn, basename)) {
      return ZX_ERR_STOP;
    }
    return ZX_OK;
  };

  fbl::unique_fd dirfd(open(dirname, O_RDONLY));
  if (!dirfd.is_valid()) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_status_t status = fdio_watch_directory(dirfd.get(), watch_func, deadline.get(),
                                            reinterpret_cast<void*>(basename));
  if (status == ZX_ERR_STOP) {
    return ZX_OK;
  }
  return status;
}

SystemInstance::SystemInstance() : SystemInstance(nullptr) {}

SystemInstance::SystemInstance(fdio_ns_t* default_ns) : default_ns_(default_ns), launcher_(this) {
  if (default_ns_ == nullptr) {
    zx_status_t status;
    status = fdio_ns_get_installed(&default_ns_);
    ZX_ASSERT_MSG(status == ZX_OK, "driver_manager: cannot get namespace: %s\n",
                  zx_status_get_string(status));
  }
}

zx_status_t SystemInstance::CreateSvcJob(const zx::job& root_job) {
  zx_status_t status = zx::job::create(root_job, 0u, &svc_job_);
  if (status != ZX_OK) {
    fprintf(stderr, "driver_manager: failed to create service job: %s\n",
            zx_status_get_string(status));
    return 1;
  }
  status = svc_job_.set_property(ZX_PROP_NAME, "zircon-services", 16);
  if (status != ZX_OK) {
    fprintf(stderr, "driver_manager: failed to set service job name: %s\n",
            zx_status_get_string(status));
    return 1;
  }

  return ZX_OK;
}

zx_status_t SystemInstance::PrepareChannels() {
  zx_status_t status;
  status = zx::channel::create(0, &miscsvc_client_, &miscsvc_server_);
  if (status != ZX_OK) {
    return status;
  }
  status = zx::channel::create(0, &device_name_provider_client_, &device_name_provider_server_);
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

zx_status_t SystemInstance::StartSvchost(const zx::job& root_job, const zx::channel& root_dir,
                                         bool require_system, Coordinator* coordinator) {
  zx::channel dir_request, svchost_local;
  zx_status_t status = zx::channel::create(0, &dir_request, &svchost_local);
  if (status != ZX_OK) {
    return status;
  }

  zx::debuglog logger;
  status = zx::debuglog::create(coordinator->root_resource(), 0, &logger);
  if (status != ZX_OK) {
    return status;
  }

  zx::job root_job_copy;
  status =
      root_job.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHTS_PROPERTY | ZX_RIGHT_ENUMERATE |
                             ZX_RIGHT_MANAGE_PROCESS | ZX_RIGHT_MANAGE_THREAD,
                         &root_job_copy);
  if (status != ZX_OK) {
    return status;
  }

  // TODO(ZX-3530): svchost needs the root resource to talk to
  // zx_debug_send_command. Remove this once zx_debug_send_command no longer
  // requires the root resource.
  zx::resource root_resource_copy;
  if (coordinator->root_resource().is_valid()) {
    status = coordinator->root_resource().duplicate(ZX_RIGHT_TRANSFER, &root_resource_copy);
    if (status != ZX_OK) {
      return status;
    }
  }

  zx::channel coordinator_client;
  {
    zx::channel coordinator_server;
    status = zx::channel::create(0, &coordinator_server, &coordinator_client);
    if (status != ZX_OK) {
      return status;
    }

    status = fdio_service_connect_at(root_dir.get(), "svc", coordinator_server.release());
    if (status != ZX_OK) {
      return status;
    }
  }

  zx::channel virtcon_client;
  status = zx::channel::create(0, &virtcon_client, &virtcon_fidl_);
  if (status != ZX_OK) {
    printf("Unable to create virtcon channel.\n");
    return status;
  }

  zx::channel miscsvc_svc;
  {
    zx::channel miscsvc_svc_req;
    status = zx::channel::create(0, &miscsvc_svc_req, &miscsvc_svc);
    if (status != ZX_OK) {
      return status;
    }

    status = fdio_service_connect_at(miscsvc_client_.get(), "svc", miscsvc_svc_req.release());
    if (status != ZX_OK) {
      return status;
    }
  }

  zx::channel device_name_provider_svc;
  {
    zx::channel device_name_provider_svc_req;
    status = zx::channel::create(0, &device_name_provider_svc_req, &device_name_provider_svc);
    if (status != ZX_OK) {
      return status;
    }

    status = fdio_service_connect_at(device_name_provider_client_.get(), "svc",
                                     device_name_provider_svc_req.release());
    if (status != ZX_OK) {
      return status;
    }
  }

  zx::channel devcoordinator_svc;
  {
    zx::channel devcoordinator_svc_req;
    status = zx::channel::create(0, &devcoordinator_svc_req, &devcoordinator_svc);
    if (status != ZX_OK) {
      return status;
    }

    // This connects to the /svc in devcoordinator's namespace.
    status = fdio_service_connect("/svc", devcoordinator_svc_req.release());
    if (status != ZX_OK) {
      return status;
    }
  }

  zx::job svc_job_copy;
  status = svc_job_.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_MANAGE_JOB | ZX_RIGHT_MANAGE_PROCESS,
                              &svc_job_copy);
  if (status != ZX_OK) {
    return status;
  }

  const char* name = "svchost";
  const char* argv[3] = {
      "/boot/bin/svchost",
      require_system ? "--require-system" : nullptr,
      nullptr,
  };

  fbl::Vector<fdio_spawn_action_t> actions;

  actions.push_back((fdio_spawn_action_t){
      .action = FDIO_SPAWN_ACTION_SET_NAME,
      .name = {.data = name},
  });

  actions.push_back((fdio_spawn_action_t){
      .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
      .h = {.id = PA_DIRECTORY_REQUEST, .handle = dir_request.release()},
  });
  actions.push_back((fdio_spawn_action_t){
      .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
      .h = {.id = PA_HND(PA_FD, FDIO_FLAG_USE_FOR_STDIO), .handle = logger.release()},
  });

  // Give svchost a restricted root job handle. svchost is already a privileged system service
  // as it controls system-wide process launching. With the root job it can consolidate a few
  // services such as crashsvc and the profile service.
  actions.push_back((fdio_spawn_action_t){
      .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
      .h = {.id = PA_HND(PA_USER0, 1), .handle = root_job_copy.release()},
  });

  // Also give svchost a restricted root resource handle, this allows it to run the kernel-debug
  // service.
  if (root_resource_copy.is_valid()) {
    actions.push_back((fdio_spawn_action_t){
        .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
        .h = {.id = PA_HND(PA_USER0, 2), .handle = root_resource_copy.release()},
    });
  }

  // Add handle to channel to allow svchost to proxy fidl services to us.
  actions.push_back((fdio_spawn_action_t){
      .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
      .h = {.id = PA_HND(PA_USER0, 3), .handle = coordinator_client.release()},
  });

  if (!coordinator->boot_args().GetBool("virtcon.disable", false)) {
    // Add handle to channel to allow svchost to proxy fidl services to
    // virtcon.
    actions.push_back((fdio_spawn_action_t){
        .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
        .h = {.id = PA_HND(PA_USER0, 5), .handle = virtcon_client.release()},
    });
  }

  // Add handle to channel to allow svchost to talk to miscsvc.
  actions.push_back((fdio_spawn_action_t){
      .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
      .h = {.id = PA_HND(PA_USER0, 6), .handle = miscsvc_svc.release()},
  });

  // Add handle to channel to allow svchost to connect to services from devcoordinator's /svc, which
  // is hosted by component_manager and includes services routed from other components; see
  // "devcoordinator.cml".
  actions.push_back((fdio_spawn_action_t){
      .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
      .h = {.id = PA_HND(PA_USER0, 7), .handle = devcoordinator_svc.release()},
  });

  // Add handle to channel to allow svchost to talk to device_name_provider.
  actions.push_back((fdio_spawn_action_t){
      .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
      .h = {.id = PA_HND(PA_USER0, 8), .handle = device_name_provider_svc.release()},
  });

  // Give svchost access to /dev/class/sysmem, to enable svchost to forward sysmem service
  // requests to the sysmem driver.  Create a namespace containing /dev/class/sysmem.
  zx::channel fs_handle = CloneFs("dev/class/sysmem");
  if (!fs_handle.is_valid()) {
    printf("driver_manager: failed to clone /dev/class/sysmem\n");
    return ZX_ERR_BAD_STATE;
  }
  actions.push_back((fdio_spawn_action_t){
      .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
      .ns = {.prefix = "/sysmem", .handle = fs_handle.release()},
  });

  char errmsg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx_handle_t proc = ZX_HANDLE_INVALID;
  status = fdio_spawn_etc(svc_job_copy.get(), FDIO_SPAWN_CLONE_JOB | FDIO_SPAWN_DEFAULT_LDSVC,
                          argv[0], argv, NULL, actions.size(), actions.data(), &proc, errmsg);
  if (status != ZX_OK) {
    printf("driver_manager: launch %s (%s) failed: %s: %d\n", argv[0], name, errmsg, status);
    return status;
  } else {
    printf("driver_manager: launch %s (%s) OK\n", argv[0], name);
  }

  zx::channel svchost_public_remote;
  status = zx::channel::create(0, &svchost_public_remote, &svchost_outgoing_);
  if (status != ZX_OK) {
    return status;
  }

  return fdio_service_connect_at(svchost_local.get(), "svc", svchost_public_remote.release());
}

zx_status_t SystemInstance::ReuseExistingSvchost() {
  // This path is only used in integration tests that start an "isolated" devmgr/devcoordinator.
  // Rather than start another svchost process - which won't work for a couple reasons - we
  // clone the /svc in devcoordinator's namespace when devcoordinator launches other processes.
  // This may or may not work well, depending on the services those processes require and whether
  // they happen to be in the /svc exposed to this test instance of devcoordinator.
  // TODO(bryanhenry): This can go away once we move the processes devcoordinator spawns today out
  // into separate components.
  zx::channel dir_request;
  zx_status_t status = zx::channel::create(0, &dir_request, &svchost_outgoing_);
  if (status != ZX_OK) {
    fprintf(stderr, "driver_manager: failed to create svchost_outgoing channel\n");
    return 1;
  }
  status = fdio_service_connect("/svc", dir_request.release());
  if (status != ZX_OK) {
    fprintf(stderr, "driver_manager: failed to connect to /svc\n");
    return 1;
  }

  return ZX_OK;
}

void SystemInstance::devmgr_vfs_init() {
  fdio_ns_t* ns;
  zx_status_t r;
  r = fdio_ns_get_installed(&ns);
  ZX_ASSERT_MSG(r == ZX_OK, "driver_manager: cannot get namespace: %s\n", zx_status_get_string(r));
  r = fdio_ns_bind(ns, "/dev", CloneFs("dev").release());
  ZX_ASSERT_MSG(r == ZX_OK, "driver_manager: cannot bind /dev to namespace: %s\n",
                zx_status_get_string(r));
}

// Thread entry point
int SystemInstance::pwrbtn_monitor_starter(void* arg) {
  auto args = std::unique_ptr<ServiceStarterArgs>(static_cast<ServiceStarterArgs*>(arg));
  return args->instance->PwrbtnMonitorStarter(args->coordinator);
}

int SystemInstance::PwrbtnMonitorStarter(Coordinator* coordinator) {
  const char* name = "pwrbtn-monitor";
  const char* argv[] = {"/boot/bin/pwrbtn-monitor", nullptr};

  zx::job job_copy;
  zx_status_t status =
      svc_job_.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_WRITE, &job_copy);
  if (status != ZX_OK) {
    printf("driver_manager: svc_job.duplicate failed %s\n", zx_status_get_string(status));
    return 1;
  }

  zx::debuglog debuglog;
  if ((status = zx::debuglog::create(coordinator->root_resource(), 0, &debuglog) != ZX_OK)) {
    printf("driver_manager: cannot create debuglog handle\n");
    return 1;
  }

  zx::channel input_handle = CloneFs("dev/class/input");
  if (!input_handle.is_valid()) {
    printf("driver_manager: failed to clone /dev/input\n");
    return 1;
  }

  zx::channel svc_handle = CloneFs("svc");
  if (!svc_handle.is_valid()) {
    printf("driver_manager: failed to clone /svc\n");
    return 1;
  }

  fdio_spawn_action_t actions[] = {
      {.action = FDIO_SPAWN_ACTION_SET_NAME, .name = {.data = name}},
      {.action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
       .ns = {.prefix = "/input", .handle = input_handle.release()}},
      // Ideally we'd only expose /svc/fuchsia.device.manager.Administrator, but we do not
      // support exposing single services.
      {.action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
       .ns = {.prefix = "/svc", .handle = svc_handle.release()}},
      {.action = FDIO_SPAWN_ACTION_ADD_HANDLE,
       .h = {.id = PA_HND(PA_FD, FDIO_FLAG_USE_FOR_STDIO | 0), .handle = debuglog.release()}},
  };

  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  uint32_t spawn_flags = FDIO_SPAWN_CLONE_JOB | FDIO_SPAWN_DEFAULT_LDSVC;
  status = fdio_spawn_etc(job_copy.get(), spawn_flags, argv[0], argv, nullptr,
                          fbl::count_of(actions), actions, nullptr, err_msg);
  if (status != ZX_OK) {
    printf("driver_manager: spawn %s (%s) failed: %s: %s\n", argv[0], name, err_msg,
           zx_status_get_string(status));
    return 1;
  }

  printf("driver_manager: launch %s (%s) OK\n", argv[0], name);
  return 0;
}

// Thread trampoline for start_console_shell/ConsoleStarter
int console_starter(void* arg) {
  auto args = std::unique_ptr<ConsoleStarterArgs>(static_cast<ConsoleStarterArgs*>(arg));
  return args->instance->ConsoleStarter(args->boot_args);
}

void SystemInstance::start_console_shell(const devmgr::BootArgs& boot_args) {
  // Only start a shell on the kernel console if it isn't already running a shell.
  if (boot_args.GetBool("kernel.shell", false)) {
    return;
  }
  auto args = std::make_unique<ConsoleStarterArgs>();
  args->instance = this;
  args->boot_args = const_cast<devmgr::BootArgs*>(&boot_args);
  thrd_t t;
  int ret = thrd_create_with_name(&t, console_starter, args.release(), "console-starter");
  if (ret == thrd_success) {
    thrd_detach(t);
  }
}

int SystemInstance::ConsoleStarter(const devmgr::BootArgs* arg) {
  auto& boot_args = *arg;

  // If we got a TERM environment variable (aka a TERM=... argument on
  // the kernel command line), pass this down; otherwise pass TERM=uart.
  const char* term = boot_args.Get("TERM");
  if (term == nullptr) {
    term = "TERM=uart";
  } else {
    term -= sizeof("TERM=") - 1;
  }

  const char* device = boot_args.Get("console.path");
  if (device == nullptr) {
    device = "/svc/console";
  }

  const char* envp[] = {
      term,
      nullptr,
  };

  // Run thread forever, relaunching console shell on exit.
  for (;;) {
    zx_status_t status = wait_for_file(device, zx::time::infinite());
    if (status != ZX_OK) {
      printf("driver_manager: failed to wait for console '%s' (%s)\n", device,
             zx_status_get_string(status));
      return 1;
    }
    fbl::unique_fd fd(open(device, O_RDWR));
    if (!fd.is_valid()) {
      printf("driver_manager: failed to open console '%s'\n", device);
      return 1;
    }

    // TODO(ZX-3385): Clean this up once devhost stops speaking fuchsia.io.File
    // on behalf of drivers.  Once that happens, the virtio-console driver
    // should just speak that instead of this shim interface.
    if (boot_args.GetBool("console.is_virtio", false)) {
      // If the console is a virtio connection, then speak the
      // fuchsia.hardware.virtioconsole.Device interface to get the real
      // fuchsia.io.File connection
      zx::channel virtio_channel;
      status = fdio_get_service_handle(fd.release(), virtio_channel.reset_and_get_address());
      if (status != ZX_OK) {
        printf("driver_manager: failed to get console handle '%s'\n", device);
        return 1;
      }

      zx::channel local, remote;
      status = zx::channel::create(0, &local, &remote);
      if (status != ZX_OK) {
        printf("driver_manager: failed to create channel for console '%s'\n", device);
        return 1;
      }

      ::llcpp::fuchsia::hardware::virtioconsole::Device::SyncClient virtio_client(
          std::move(virtio_channel));
      virtio_client.GetChannel(std::move(remote));

      fdio_t* fdio;
      status = fdio_create(local.release(), &fdio);
      if (status != ZX_OK) {
        printf("driver_manager: failed to setup fdio for console '%s'\n", device);
        return 1;
      }

      fd.reset(fdio_bind_to_fd(fdio, -1, 3));
      if (!fd.is_valid()) {
        fdio_unsafe_release(fdio);
        printf("driver_manager: failed to transfer fdio for console '%s'\n", device);
        return 1;
      }
    }

    zx::channel ldsvc;
    status = clone_fshost_ldsvc(&ldsvc);
    if (status != ZX_OK) {
      fprintf(stderr, "driver_manager: failed to clone fshost loader for console: %d\n", status);
      return 1;
    }

    const char* argv_sh[] = {ZX_SHELL_DEFAULT, nullptr};
    zx::process proc;
    status = launcher_.LaunchWithLoader(svc_job_, "sh:console", zx::vmo(), std::move(ldsvc),
                                        argv_sh, envp, fd.release(), zx::resource(), nullptr,
                                        nullptr, 0, &proc, FS_ALL);
    if (status != ZX_OK) {
      printf("driver_manager: failed to launch console shell (%s)\n", zx_status_get_string(status));
      return 1;
    }

    status = proc.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
    if (status != ZX_OK) {
      printf("driver_manager: failed to wait for console shell termination (%s)\n",
             zx_status_get_string(status));
      return 1;
    }
    zx_info_process_t proc_info;
    status = proc.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr);
    if (status != ZX_OK) {
      printf("driver_manager: failed to determine console shell termination cause (%s)\n",
             zx_status_get_string(status));
      return 1;
    }
    printf(
        "driver_manager: console shell exited (started=%d exited=%d, return_code=%ld), "
        "restarting\n",
        proc_info.started, proc_info.exited, proc_info.return_code);
  }
  /* NOTREACHED */
  return 0;
}

// Thread trampoline for ServiceStarter
int SystemInstance::service_starter(void* arg) {
  auto args = std::unique_ptr<ServiceStarterArgs>(static_cast<ServiceStarterArgs*>(arg));
  return args->instance->ServiceStarter(args->coordinator);
}

// Thread trampoline for WaitForSystemAvailable, which ServiceStarter spawns
int wait_for_system_available(void* arg) {
  auto args = std::unique_ptr<SystemInstance::ServiceStarterArgs>(
      static_cast<SystemInstance::ServiceStarterArgs*>(arg));
  return args->instance->WaitForSystemAvailable(args->coordinator);
}

int SystemInstance::ServiceStarter(Coordinator* coordinator) {
  // Launch miscsvc binary with access to:
  // * /dev to talk to hardware
  // * /boot to dynamically load drivers (zxcrypt)
  // * /svc to call launch processes (minfs)
  // * /volume to mount (minfs)
  const zx_handle_t handles[] = {miscsvc_server_.release()};
  const uint32_t types[] = {PA_DIRECTORY_REQUEST};
  const char* args[] = {"/boot/bin/miscsvc", nullptr};

  {
    // TODO(34633): miscsvc needs access to /boot/lib/asan when devcoordinator runs in isolated
    // devmgr mode.
    zx::channel ldsvc;
    zx_status_t status = clone_fshost_ldsvc(&ldsvc);
    if (status != ZX_OK) {
      fprintf(stderr, "driver_manager: failed to clone loader for miscsvc: %d\n", status);
      return -1;
    }

    launcher_.LaunchWithLoader(svc_job_, "miscsvc", zx::vmo(), std::move(ldsvc), args, nullptr, -1,
                               coordinator->root_resource(), handles, types, countof(handles),
                               nullptr, FS_BOOT | FS_DEV | FS_SVC | FS_VOLUME);
  }

  bool netboot = false;
  bool vruncmd = false;
  fbl::String vcmd;
  const char* interface = coordinator->boot_args().Get("netsvc.interface");
  if (!(coordinator->boot_args().GetBool("netsvc.disable", true) ||
        coordinator->disable_netsvc())) {
    const char* args[] = {"/boot/bin/netsvc", nullptr, nullptr, nullptr, nullptr, nullptr};
    int argc = 1;

    if (coordinator->boot_args().GetBool("netsvc.netboot", false)) {
      args[argc++] = "--netboot";
      netboot = true;
      vruncmd = true;
    }

    if (coordinator->boot_args().GetBool("netsvc.advertise", true)) {
      args[argc++] = "--advertise";
    }

    if (coordinator->boot_args().GetBool("netsvc.all-features", false)) {
      args[argc++] = "--all-features";
    }

    if (interface != nullptr) {
      args[argc++] = "--interface";
      args[argc++] = interface;
    }

    zx::process proc;
    zx_status_t status =
        launcher_.Launch(svc_job_, "netsvc", args, nullptr, -1, coordinator->root_resource(),
                         nullptr, nullptr, 0, &proc, FS_ALL);
    if (status == ZX_OK) {
      if (vruncmd) {
        zx_info_handle_basic_t info = {};
        proc.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
        proc.reset();
        vcmd = fbl::StringPrintf("dlog -f -t -p %zu", info.koid);
      }
    } else {
      vruncmd = false;
    }
    __UNUSED auto leaked_handle = proc.release();
  }

  if (!coordinator->disable_netsvc()) {
    // Launch device-name-provider with access to /dev, to discover network interfaces.
    const zx_handle_t handles[] = {device_name_provider_server_.release()};
    const uint32_t types[] = {PA_DIRECTORY_REQUEST};
    const char* nodename = coordinator->boot_args().Get("zircon.nodename");
    const char* args[] = {
        "/boot/bin/device-name-provider", nullptr, nullptr, nullptr, nullptr, nullptr};
    int argc = 1;

    if (interface != nullptr) {
      args[argc++] = "--interface";
      args[argc++] = interface;
    }

    if (nodename != nullptr) {
      args[argc++] = "--nodename";
      args[argc++] = nodename;
    }

    launcher_.Launch(svc_job_, "device-name-provider", args, nullptr, -1,
                     coordinator->root_resource(), handles, types, countof(handles), nullptr,
                     FS_DEV);
  }

  if (!coordinator->boot_args().GetBool("virtcon.disable", false)) {
    // pass virtcon.* options along
    fbl::Vector<const char*> env;
    coordinator->boot_args().Collect("virtcon.", &env);
    env.push_back(nullptr);

    const char* num_shells = coordinator->require_system() && !netboot ? "0" : "3";
    size_t handle_count = 0;
    zx_handle_t handles[2];
    uint32_t types[2];

    handles[handle_count] = virtcon_fidl_.release();
    types[handle_count] = PA_HND(PA_USER0, 0);
    ++handle_count;

    zx::debuglog debuglog;
    zx_status_t status =
        zx::debuglog::create(coordinator->root_resource(), ZX_LOG_FLAG_READABLE, &debuglog);
    if (status == ZX_OK) {
      handles[handle_count] = debuglog.release();
      types[handle_count] = PA_HND(PA_USER0, 1);
      ++handle_count;
    }

    const char* args[] = {
        "/boot/bin/virtual-console", "--shells", num_shells, nullptr, nullptr, nullptr};
    if (vruncmd) {
      args[3] = "--run";
      args[4] = vcmd.data();
    }
    launcher_.Launch(svc_job_, "virtual-console", args, env.data(), -1,
                     coordinator->root_resource(), handles, types, handle_count, nullptr, FS_ALL);
  }

  const char* backstop = coordinator->boot_args().Get("clock.backstop");
  if (backstop) {
    zx_time_t offset = ZX_SEC(atoi(backstop));
    printf("driver_manager: setting UTC backstop: %ld\n", offset);
    zx_clock_adjust(coordinator->root_resource().get(), ZX_CLOCK_UTC, offset);
  }

  do_autorun("autorun:boot", coordinator->boot_args().Get("zircon.autorun.boot"),
             coordinator->root_resource());

  auto starter_args = std::make_unique<ServiceStarterArgs>();
  starter_args->instance = this;
  starter_args->coordinator = coordinator;
  thrd_t t;
  int ret = thrd_create_with_name(&t, wait_for_system_available, starter_args.release(),
                                  "wait-for-system-available");
  if (ret == thrd_success) {
    thrd_detach(t);
  }

  return 0;
}

int SystemInstance::WaitForSystemAvailable(Coordinator* coordinator) {
  // Block this thread until /system-delayed is available. Note that this is
  // only used for coordinating events between fshost and devcoordinator, the
  // /system path is used for loading drivers and appmgr below.
  // TODO: It's pretty wasteful to create a thread just so it can sit blocked in
  // sync I/O opening '/system-delayed'. Once a simple async I/O wrapper exists
  // this should switch to use that
  int fd = open("/system-delayed", O_RDONLY);
  if (!fd) {
    fprintf(stderr,
            "driver_manager: failed to open /system-delayed! System drivers and autorun:system "
            "won't work!\n");
    return 1;
  }
  close(fd);

  // Load in drivers from /system
  coordinator->set_system_available(true);
  coordinator->ScanSystemDrivers();

  do_autorun("autorun:system", coordinator->boot_args().Get("zircon.autorun.system"),
             coordinator->root_resource());

  return 0;
}

// TODO(ZX-4860): DEPRECATED. Do not add new dependencies on the fshost loader service!
zx_status_t SystemInstance::clone_fshost_ldsvc(zx::channel* loader) {
  zx::channel remote;
  zx_status_t status = zx::channel::create(0, loader, &remote);
  if (status != ZX_OK) {
    return status;
  }
  return fdio_service_connect("/svc/fuchsia.fshost.Loader", remote.release());
}

void SystemInstance::do_autorun(const char* name, const char* cmd,
                                const zx::resource& root_resource) {
  if (cmd != nullptr) {
    auto args = ArgumentVector::FromCmdline(cmd);
    args.Print("autorun");

    zx::channel ldsvc;
    zx_status_t status = clone_fshost_ldsvc(&ldsvc);
    if (status != ZX_OK) {
      fprintf(stderr, "driver_manager: failed to clone fshost loader for console: %d\n", status);
      return;
    }

    status = launcher_.LaunchWithLoader(svc_job_, name, zx::vmo(), std::move(ldsvc), args.argv(),
                                        nullptr, -1, root_resource, nullptr, nullptr, 0, nullptr,
                                        FS_ALL);
    if (status != ZX_OK) {
      fprintf(stderr, "driver_manager: autorun \"%s\" failed: %s\n", name,
              zx_status_get_string(status));
    }
  }
}

zx::channel SystemInstance::CloneFs(const char* path) {
  if (!strcmp(path, "dev")) {
    return devfs_root_clone();
  }
  zx::channel h0, h1;
  if (zx::channel::create(0, &h0, &h1) != ZX_OK) {
    return zx::channel();
  }
  zx_status_t status = ZX_OK;
  if (!strcmp(path, "svc")) {
    zx::unowned_channel fs = zx::unowned_channel(svchost_outgoing_);
    status = fdio_service_clone_to(fs->get(), h1.release());
  } else if (!strncmp(path, "dev/", 4)) {
    zx::unowned_channel fs = devfs_root_borrow();
    path += 4;
    status = fdio_open_at(fs->get(), path, FS_READ_WRITE_DIR_FLAGS, h1.release());
  }
  if (status != ZX_OK) {
    log(ERROR, "driver_manager: CloneFs failed for path %s: %s\n", path,
        zx_status_get_string(status));
    return zx::channel();
  }
  return h0;
}
