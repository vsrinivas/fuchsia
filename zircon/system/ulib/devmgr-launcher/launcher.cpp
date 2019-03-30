// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/devmgr-launcher/launch.h>

#include <stdint.h>
#include <utility>

#include <fbl/algorithm.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/spawn.h>
#include <lib/zx/channel.h>
#include <lib/zx/process.h>
#include <zircon/assert.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <lib/devmgr-launcher/processargs.h>

namespace {

constexpr const char* kDevmgrPath = "/boot/bin/devcoordinator";

} // namespace

namespace devmgr_launcher {

zx_status_t Launch(Args args, zx::channel bootsvc_client, zx::job* devmgr_job,
                   zx::channel* devfs_root) {
    // Create containing job (and copy to send to devmgr)
    zx::job job, job_copy;
    zx_status_t status = zx::job::create(*zx::job::default_job(), 0, &job);
    if (status != ZX_OK) {
        return status;
    }
    status = job.duplicate(ZX_RIGHT_SAME_RIGHTS, &job_copy);
    if (status != ZX_OK) {
        return status;
    }

    // Create a new client to /boot to give to devmgr
    zx::channel bootfs_client;
    {
        zx::channel bootfs_server;
        status = zx::channel::create(0, &bootfs_client, &bootfs_server);
        if (status != ZX_OK) {
            return status;
        }

        fdio_ns_t* ns;
        status = fdio_ns_get_installed(&ns);
        if (status != ZX_OK) {
            return status;
        }
        status = fdio_ns_connect(ns, "/boot", ZX_FS_RIGHT_READABLE, bootfs_server.release());
        if (status != ZX_OK) {
            return status;
        }
    }

    // Create a new client to /svc to maybe give to devmgr
    zx::channel svc_client;
    {
        zx::channel svc_server;
        status = zx::channel::create(0, &svc_client, &svc_server);
        if (status != ZX_OK) {
            return status;
        }

        fdio_ns_t* ns;
        status = fdio_ns_get_installed(&ns);
        if (status != ZX_OK) {
            return status;
        }
        status = fdio_ns_connect(ns, "/svc", ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE,
                                 svc_server.release());
        if (status != ZX_OK) {
            return status;
        }
    }

    // Create channel to connect to devfs
    zx::channel devfs_client, devfs_server;
    status = zx::channel::create(0, &devfs_client, &devfs_server);
    if (status != ZX_OK) {
        return status;
    }

    const bool clone_stdio = !args.stdio.is_valid();

    fbl::Vector<const char*> argv;
    argv.push_back(kDevmgrPath);
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
    if (args.use_system_svchost) {
        argv.push_back("--use-system-svchost");
    }
    if (args.disable_block_watcher) {
        argv.push_back("--disable-block-watcher");
    }
    argv.push_back(nullptr);

    fbl::Vector<fdio_spawn_action_t> actions;
    actions.push_back(fdio_spawn_action_t{
        .action = FDIO_SPAWN_ACTION_SET_NAME,
        .name = {.data = "test-devmgr"},
    });
    actions.push_back(fdio_spawn_action_t{
        .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
        .h = {.id = PA_HND(PA_JOB_DEFAULT, 0), .handle = job_copy.release()},
    });
    actions.push_back(fdio_spawn_action_t{
        .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
        .h = {.id = DEVMGR_LAUNCHER_DEVFS_ROOT_HND, .handle = devfs_server.release()},
    });
    actions.push_back(fdio_spawn_action_t{
        .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
        .ns = {.prefix = "/boot", .handle = bootfs_client.release()},
    });
    actions.push_back(fdio_spawn_action_t{
        .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
        .ns = {.prefix = "/bootsvc", .handle = bootsvc_client.release()},
    });
    if (args.use_system_svchost) {
        actions.push_back(fdio_spawn_action_t{
            .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
            .ns = {.prefix = "/svc", .handle = svc_client.release()},
        });
    }
    if (!clone_stdio) {
        actions.push_back(fdio_spawn_action_t{
            .action = FDIO_SPAWN_ACTION_TRANSFER_FD,
            .fd = {.local_fd = args.stdio.release(), .target_fd = FDIO_FLAG_USE_FOR_STDIO},
        });
    }

    uint32_t flags = FDIO_SPAWN_DEFAULT_LDSVC;
    if (clone_stdio) {
        flags |= FDIO_SPAWN_CLONE_STDIO;
    }

    zx::process new_process;
    status = fdio_spawn_etc(job.get(),
                            flags,
                            kDevmgrPath,
                            argv.get(),
                            nullptr /* environ */,
                            actions.size(),
                            actions.get(),
                            new_process.reset_and_get_address(),
                            nullptr /* err_msg */);
    if (status != ZX_OK) {
        return status;
    }

    *devmgr_job = std::move(job);
    *devfs_root = std::move(devfs_client);
    return ZX_OK;
}

} // namespace devmgr_launcher
