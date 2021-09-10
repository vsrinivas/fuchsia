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
#include <lib/fdio/spawn-actions.h>
#include <lib/fdio/spawn.h>
#include <lib/zx/channel.h>
#include <lib/zx/process.h>
#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <utility>
#include <vector>

#include <fbl/algorithm.h>

namespace {

constexpr const char* kDevmgrPath = "/pkg/bin/driver_manager";
constexpr const char* kFshostPath = "/pkg/bin/fshost";
constexpr const char* kDriverIndexPath = "/pkg/bin/driver_index";

}  // namespace

namespace devmgr_launcher {

zx_status_t LaunchDriverIndex(const Args& args, zx::job& job, zx::channel svc_client,
                              zx::channel outgoing_svc_dir) {
  zx::job job_copy;
  zx_status_t status = job.duplicate(ZX_RIGHT_SAME_RIGHTS, &job_copy);
  if (status != ZX_OK) {
    return status;
  }

  const bool clone_stdio = !args.stdio.is_valid();

  std::vector<const char*> argv;
  argv.push_back(kDriverIndexPath);
  argv.push_back("--no-base-drivers");
  argv.push_back(nullptr);

  FdioSpawnActions actions;
  actions.AddAction(fdio_spawn_action_t{
      .action = FDIO_SPAWN_ACTION_SET_NAME,
      .name = {.data = "test-driver-index"},
  });
  actions.AddActionWithHandle(
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
          .h = {.id = PA_HND(PA_JOB_DEFAULT, 0)},
      },
      std::move(job_copy));
  actions.AddActionWithHandle(
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
          .h = {.id = PA_HND(PA_DIRECTORY_REQUEST, 0)},
      },
      std::move(outgoing_svc_dir));

  zx::channel local, remote;
  status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }
  status = fdio_open("/pkg", ZX_FS_RIGHT_READABLE | ZX_FS_FLAG_DIRECTORY, remote.release());
  if (status != ZX_OK) {
    return status;
  }
  actions.AddActionWithNamespace(
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
          .ns = {.prefix = "/boot"},
      },
      std::move(local));

  actions.AddActionWithNamespace(
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
          .ns = {.prefix = "/svc"},
      },
      std::move(svc_client));

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

    actions.AddAction(fdio_spawn_action_t{
        .action = FDIO_SPAWN_ACTION_TRANSFER_FD,
        .fd = {.local_fd = stdio_clone_fd, .target_fd = FDIO_FLAG_USE_FOR_STDIO},
    });
  }

  uint32_t flags = FDIO_SPAWN_DEFAULT_LDSVC | FDIO_SPAWN_CLONE_UTC_CLOCK;
  if (clone_stdio) {
    flags |= FDIO_SPAWN_CLONE_STDIO;
  }

  zx::process new_process;
  std::vector<fdio_spawn_action_t> spawn_actions = actions.GetActions();
  status = fdio_spawn_etc(job.get(), flags, kDriverIndexPath, argv.data(), nullptr /* environ */,
                          spawn_actions.size(), spawn_actions.data(),
                          new_process.reset_and_get_address(), nullptr /* err_msg */);
  if (status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

// Launches an fshost process in the given job. Fshost will have devfs_client
// installed in its namespace as /dev, and svc_client as /svc
zx_status_t LaunchFshost(const Args& args, zx::job& job, zx::channel svc_client,
                         zx::channel fshost_outgoing_server, zx::channel devfs_client) {
  zx::job job_copy;
  zx_status_t status = job.duplicate(ZX_RIGHT_SAME_RIGHTS, &job_copy);
  if (status != ZX_OK) {
    return status;
  }

  const bool clone_stdio = !args.stdio.is_valid();

  std::vector<const char*> argv;
  argv.push_back(kFshostPath);
  if (args.disable_block_watcher) {
    argv.push_back("--disable-block-watcher");
  }
  argv.push_back(nullptr);

  FdioSpawnActions actions;
  actions.AddAction(fdio_spawn_action_t{
      .action = FDIO_SPAWN_ACTION_SET_NAME,
      .name = {.data = "test-fshost"},
  });
  actions.AddActionWithHandle(
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
          .h = {.id = PA_HND(PA_JOB_DEFAULT, 0)},
      },
      std::move(job_copy));
  actions.AddActionWithHandle(
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
          .h = {.id = PA_HND(PA_DIRECTORY_REQUEST, 0)},
      },
      std::move(fshost_outgoing_server));

  actions.AddActionWithNamespace(
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
          .ns = {.prefix = "/dev"},
      },
      std::move(devfs_client));

  zx::channel local, remote;
  status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }
  status = fdio_open("/pkg", ZX_FS_RIGHT_READABLE | ZX_FS_FLAG_DIRECTORY, remote.release());
  if (status != ZX_OK) {
    return status;
  }
  actions.AddActionWithNamespace(
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
          .ns = {.prefix = "/boot"},
      },
      std::move(local));

  actions.AddActionWithNamespace(
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
          .ns = {.prefix = "/svc"},
      },
      std::move(svc_client));

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

    actions.AddAction(fdio_spawn_action_t{
        .action = FDIO_SPAWN_ACTION_TRANSFER_FD,
        .fd = {.local_fd = stdio_clone_fd, .target_fd = FDIO_FLAG_USE_FOR_STDIO},
    });
  }

  uint32_t flags = FDIO_SPAWN_DEFAULT_LDSVC | FDIO_SPAWN_CLONE_UTC_CLOCK;
  if (clone_stdio) {
    flags |= FDIO_SPAWN_CLONE_STDIO;
  }

  zx::process new_process;
  std::vector<fdio_spawn_action_t> spawn_actions = actions.GetActions();
  status = fdio_spawn_etc(job.get(), flags, kFshostPath, argv.data(), nullptr /* environ */,
                          spawn_actions.size(), spawn_actions.data(),
                          new_process.reset_and_get_address(), nullptr /* err_msg */);
  if (status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

zx_status_t LaunchDriverManager(const Args& args, zx::job& job, zx::channel devfs_server,
                                zx::channel outgoing_services_server,
                                zx::channel component_lifecycle_server, zx::channel svc_client,
                                zx::process* new_process) {
  zx::job devmgr_job_copy;
  zx_status_t status = job.duplicate(ZX_RIGHT_SAME_RIGHTS, &devmgr_job_copy);
  if (status != ZX_OK) {
    return status;
  }
  const bool clone_stdio = !args.stdio.is_valid();

  std::vector<const char*> argv;
  argv.push_back(kDevmgrPath);

  // Driver-Index setup.
  if (!args.disable_driver_index) {
    argv.push_back("--use-driver-index");
    if (args.sys_device_driver == nullptr) {
      argv.push_back("--sys-device-driver");
      argv.push_back("fuchsia-boot:///#driver/platform-bus.so");
    } else {
      // If we are using the driver-index and the sys_device_driver is a path,
      // then we have to specifically load that driver.
      if (args.sys_device_driver[0] == '/') {
        argv.push_back("--load-driver");
        argv.push_back(args.sys_device_driver);
      }
      // If we're loading the old platform-bus we have to also load its proxy.
      if (strcmp(args.sys_device_driver, "/boot/driver/platform-bus.so") == 0) {
        argv.push_back("--load-driver");
        argv.push_back("/boot/driver/platform-bus.proxy.so");
      }
    }
  }

  for (const char* path : args.load_drivers) {
    argv.push_back("--load-driver");
    argv.push_back(path);
  }
  if (args.no_exit_after_suspend) {
    argv.push_back("--no-exit-after-suspend");
  }
  if (args.sys_device_driver != nullptr) {
    argv.push_back("--sys-device-driver");
    argv.push_back(args.sys_device_driver);
  }
  if (args.driver_runner_root_driver_url != nullptr) {
    argv.push_back("--driver-runner-root-driver-url");
    argv.push_back(args.driver_runner_root_driver_url);
  }
  argv.push_back(nullptr);

  FdioSpawnActions actions;
  actions.AddAction(fdio_spawn_action_t{
      .action = FDIO_SPAWN_ACTION_SET_NAME,
      .name = {.data = "test-devmgr"},
  });
  actions.AddActionWithHandle(
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
          .h = {.id = PA_HND(PA_JOB_DEFAULT, 0)},
      },
      std::move(devmgr_job_copy));
  actions.AddActionWithHandle(
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
          .h = {.id = DEVMGR_LAUNCHER_DEVFS_ROOT_HND},
      },
      std::move(devfs_server));
  actions.AddActionWithHandle(
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
          .h = {.id = DEVMGR_LAUNCHER_OUTGOING_SERVICES_HND},
      },
      std::move(outgoing_services_server));
  actions.AddActionWithHandle(
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
          .h = {.id = PA_LIFECYCLE},
      },
      std::move(component_lifecycle_server));

  for (auto& ns : args.flat_namespace) {
    zx::handle ns_handle_clone(fdio_service_clone(ns.second.get()));
    actions.AddActionWithNamespace(
        fdio_spawn_action_t{
            .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
            .ns = {.prefix = ns.first},
        },
        std::move(ns_handle_clone));
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

  actions.AddActionWithNamespace(
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
          .ns = {.prefix = "/boot"},
      },
      std::move(local));

  actions.AddActionWithNamespace(
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
          .ns = {.prefix = "/svc"},
      },
      std::move(svc_client));

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

    actions.AddAction(fdio_spawn_action_t{
        .action = FDIO_SPAWN_ACTION_TRANSFER_FD,
        .fd = {.local_fd = stdio_clone_fd, .target_fd = FDIO_FLAG_USE_FOR_STDIO},
    });
  }

  uint32_t flags = FDIO_SPAWN_DEFAULT_LDSVC | FDIO_SPAWN_CLONE_UTC_CLOCK;
  if (clone_stdio) {
    flags |= FDIO_SPAWN_CLONE_STDIO;
  }

  std::vector<fdio_spawn_action_t> spawn_actions = actions.GetActions();
  status = fdio_spawn_etc(job.get(), flags, kDevmgrPath, argv.data(), nullptr /* environ */,
                          spawn_actions.size(), spawn_actions.data(),
                          new_process->reset_and_get_address(), nullptr /* err_msg */);
  if (status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

__EXPORT
zx_status_t Launch(Args args, zx::channel svc_client, zx::channel fshost_outgoing_server,
                   zx::channel driver_index_outgoing_server, zx::channel component_lifecycle_server,
                   zx::job* devmgr_job, zx::process* devmgr_process, zx::channel* devfs_root,
                   zx::channel* outgoing_services_root) {
  zx::job job;
  zx_status_t status = zx::job::create(*zx::job::default_job(), 0, &job);
  if (status != ZX_OK) {
    return status;
  }

  // Create channel to connect to devfs
  zx::channel devfs_client, devfs_server;
  status = zx::channel::create(0, &devfs_client, &devfs_server);
  if (status != ZX_OK) {
    return status;
  }

  // Create channel to connect to outgoing services
  zx::channel outgoing_services_client, outgoing_services_server;
  status = zx::channel::create(0, &outgoing_services_client, &outgoing_services_server);
  if (status != ZX_OK) {
    return status;
  }

  zx::process new_process;

  // Launch driver manager.
  {
    zx::channel svc_client_for_dm(fdio_service_clone(svc_client.get()));
    status = LaunchDriverManager(
        args, job, std::move(devfs_server), std::move(outgoing_services_server),
        std::move(component_lifecycle_server), std::move(svc_client_for_dm), &new_process);
    if (status != ZX_OK) {
      return status;
    }
  }

  // Launch driver_index.
  {
    zx::channel svc_client_for_index(fdio_service_clone(svc_client.get()));
    status = LaunchDriverIndex(args, job, std::move(svc_client_for_index),
                               std::move(driver_index_outgoing_server));
    if (status != ZX_OK) {
      return status;
    }
  }

  // Launch fshost.
  {
    zx::channel devfs_for_fshost(fdio_service_clone(devfs_client.get()));
    status = LaunchFshost(args, job, std::move(svc_client), std::move(fshost_outgoing_server),
                          std::move(devfs_for_fshost));
    if (status != ZX_OK) {
      return status;
    }
  }

  *devmgr_job = std::move(job);
  *devmgr_process = std::move(new_process);
  *devfs_root = std::move(devfs_client);
  *outgoing_services_root = std::move(outgoing_services_client);
  return ZX_OK;
}

}  // namespace devmgr_launcher
