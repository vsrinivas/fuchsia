// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/devmgr-launcher/launch.h>
#include <lib/devmgr-launcher/processargs.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/spawn.h>
#include <lib/zx/channel.h>
#include <lib/zx/process.h>
#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <utility>

#include <fbl/algorithm.h>

namespace {

constexpr const char* kDevmgrPath = "/pkg/bin/driver_manager";
constexpr const char* kFshostPath = "/pkg/bin/fshost";

}  // namespace

namespace devmgr_launcher {

// Launches an fshost process in the given job. Fshost will have devfs_client
// installed in its namespace as /dev, and svc_client as /svc
zx_status_t LaunchFshost(Args args, zx::channel svc_client, zx::channel fshost_outgoing_server,
                         zx::job devmgr_job, zx::channel devfs_client) {
  zx::job job_copy;
  zx_status_t status = devmgr_job.duplicate(ZX_RIGHT_SAME_RIGHTS, &job_copy);
  if (status != ZX_OK) {
    return status;
  }

  const bool clone_stdio = !args.stdio.is_valid();

  fbl::Vector<const char*> argv;
  argv.push_back(kFshostPath);
  if (args.disable_block_watcher) {
    argv.push_back("--disable-block-watcher");
  }
  argv.push_back(nullptr);

  fbl::Vector<fdio_spawn_action_t> actions;
  actions.push_back(fdio_spawn_action_t{
      .action = FDIO_SPAWN_ACTION_SET_NAME,
      .name = {.data = "test-fshost"},
  });
  actions.push_back(fdio_spawn_action_t{
      .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
      .h = {.id = PA_HND(PA_JOB_DEFAULT, 0), .handle = job_copy.release()},
  });
  actions.push_back(fdio_spawn_action_t{
      .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
      .h = {.id = PA_HND(PA_DIRECTORY_REQUEST, 0), .handle = fshost_outgoing_server.release()},
  });

  actions.push_back(fdio_spawn_action_t{
      .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
      .ns = {.prefix = "/dev", .handle = devfs_client.release()},
  });

  zx::channel local, remote;
  status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }
  status = fdio_open("/pkg", ZX_FS_RIGHT_READABLE | ZX_FS_FLAG_DIRECTORY, remote.release());
  if (status != ZX_OK) {
    return status;
  }
  actions.push_back(fdio_spawn_action_t{
      .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
      .ns = {.prefix = "/boot", .handle = local.release()},
  });

  actions.push_back(fdio_spawn_action_t{
      .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
      .ns = {.prefix = "/svc", .handle = svc_client.release()},
  });

  if (!clone_stdio) {
    actions.push_back(fdio_spawn_action_t{
        .action = FDIO_SPAWN_ACTION_TRANSFER_FD,
        .fd = {.local_fd = args.stdio.release(), .target_fd = FDIO_FLAG_USE_FOR_STDIO},
    });
  }

  uint32_t flags = FDIO_SPAWN_DEFAULT_LDSVC | FDIO_SPAWN_CLONE_UTC_CLOCK;
  if (clone_stdio) {
    flags |= FDIO_SPAWN_CLONE_STDIO;
  }

  zx::process new_process;
  status = fdio_spawn_etc(devmgr_job.get(), flags, kFshostPath, argv.data(), nullptr /* environ */,
                          actions.size(), actions.data(), new_process.reset_and_get_address(),
                          nullptr /* err_msg */);
  if (status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

__EXPORT
zx_status_t Launch(Args args, zx::channel svc_client, zx::channel fshost_outgoing_server,
                   zx::channel component_lifecycle_server, zx::job* devmgr_job,
                   zx::channel* devfs_root, zx::channel* outgoing_services_root) {
  // Create containing job (and copies for devcoordinator and fshost)
  zx::job job, devmgr_job_copy, fshost_job_copy;
  zx_status_t status = zx::job::create(*zx::job::default_job(), 0, &job);
  if (status != ZX_OK) {
    return status;
  }
  status = job.duplicate(ZX_RIGHT_SAME_RIGHTS, &devmgr_job_copy);
  if (status != ZX_OK) {
    return status;
  }
  status = job.duplicate(ZX_RIGHT_SAME_RIGHTS, &fshost_job_copy);
  if (status != ZX_OK) {
    return status;
  }

  // Create channel to connect to devfs
  zx::channel devfs_client, devfs_server;
  status = zx::channel::create(0, &devfs_client, &devfs_server);
  if (status != ZX_OK) {
    return status;
  }

  // Create devfs client clone for fshost
  zx::channel devfs_for_fshost(fdio_service_clone(devfs_client.get()));

  // Create svc client clone for fshost
  zx::channel svc_client_for_fshost(fdio_service_clone(svc_client.get()));

  // Create channel to connect to outgoing services
  zx::channel outgoing_services_client, outgoing_services_server;
  status = zx::channel::create(0, &outgoing_services_client, &outgoing_services_server);
  if (status != ZX_OK) {
    return status;
  }

  const bool clone_stdio = !args.stdio.is_valid();

  fbl::Vector<const char*> argv;
  argv.push_back(kDevmgrPath);
  argv.push_back("--no-start-svchost");
  for (const char* path : args.driver_search_paths) {
    argv.push_back("--driver-search-path");
    argv.push_back(path);
  }
  for (const char* path : args.load_drivers) {
    argv.push_back("--load-driver");
    argv.push_back(path);
  }
  if (args.sys_device_driver != nullptr) {
    argv.push_back("--sys-device-driver");
    argv.push_back(args.sys_device_driver);
  }
  if (args.disable_netsvc) {
    argv.push_back("--disable-netsvc");
  }
  if (args.no_exit_after_suspend) {
    argv.push_back("--no-exit-after-suspend");
  }
  argv.push_back(nullptr);

  fbl::Vector<fdio_spawn_action_t> actions;
  actions.push_back(fdio_spawn_action_t{
      .action = FDIO_SPAWN_ACTION_SET_NAME,
      .name = {.data = "test-devmgr"},
  });
  actions.push_back(fdio_spawn_action_t{
      .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
      .h = {.id = PA_HND(PA_JOB_DEFAULT, 0), .handle = devmgr_job_copy.release()},
  });
  actions.push_back(fdio_spawn_action_t{
      .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
      .h = {.id = DEVMGR_LAUNCHER_DEVFS_ROOT_HND, .handle = devfs_server.release()},
  });
  actions.push_back(fdio_spawn_action_t{
      .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
      .h = {.id = DEVMGR_LAUNCHER_OUTGOING_SERVICES_HND,
            .handle = outgoing_services_server.release()},
  });
  actions.push_back(fdio_spawn_action_t{
      .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
      .h = {.id = PA_LIFECYCLE, .handle = component_lifecycle_server.release()},
  });

  for (auto& ns : args.flat_namespace) {
    zx_handle_t ns_handle_clone = fdio_service_clone(ns.second.get());
    actions.push_back(fdio_spawn_action_t{
        .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
        .ns = {.prefix = ns.first, .handle = ns_handle_clone},
    });
  }

  zx::channel local, remote;
  status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }
  status = fdio_open("/pkg", ZX_FS_RIGHT_READABLE | ZX_FS_FLAG_DIRECTORY, remote.release());
  if (status != ZX_OK) {
    return status;
  }

  actions.push_back(fdio_spawn_action_t{
      .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
      .ns = {.prefix = "/boot", .handle = local.release()},
  });

  actions.push_back(fdio_spawn_action_t{
      .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
      .ns = {.prefix = "/svc", .handle = svc_client.release()},
  });

  if (!clone_stdio) {
    zx_handle_t stdio_clone;
    status = fdio_fd_clone(args.stdio.get(), &stdio_clone);
    if (status != ZX_OK) {
      return status;
    }

    fdio_t* stdio_clone_fdio_t;
    status = fdio_create(stdio_clone, &stdio_clone_fdio_t);
    if (status != ZX_OK) {
      return status;
    }

    int stdio_clone_fd = fdio_bind_to_fd(stdio_clone_fdio_t, -1, 0);

    actions.push_back(fdio_spawn_action_t{
        .action = FDIO_SPAWN_ACTION_TRANSFER_FD,
        .fd = {.local_fd = stdio_clone_fd, .target_fd = FDIO_FLAG_USE_FOR_STDIO},
    });
  }

  uint32_t flags = FDIO_SPAWN_DEFAULT_LDSVC | FDIO_SPAWN_CLONE_UTC_CLOCK;
  if (clone_stdio) {
    flags |= FDIO_SPAWN_CLONE_STDIO;
  }

  zx::process new_process;
  status = fdio_spawn_etc(job.get(), flags, kDevmgrPath, argv.data(), nullptr /* environ */,
                          actions.size(), actions.data(), new_process.reset_and_get_address(),
                          nullptr /* err_msg */);
  if (status != ZX_OK) {
    return status;
  }

  status = LaunchFshost(std::move(args), std::move(svc_client_for_fshost),
                        std::move(fshost_outgoing_server), std::move(fshost_job_copy),
                        std::move(devfs_for_fshost));
  if (status != ZX_OK) {
    return status;
  }

  *devmgr_job = std::move(job);
  *devfs_root = std::move(devfs_client);
  *outgoing_services_root = std::move(outgoing_services_client);
  return ZX_OK;
}

}  // namespace devmgr_launcher
