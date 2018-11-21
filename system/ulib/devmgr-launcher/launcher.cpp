// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/devmgr-launcher/launch.h>

#include <stdint.h>
#include <utility>

#include <fbl/algorithm.h>
#include <launchpad/launchpad.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/util.h>
#include <lib/zx/channel.h>
#include <zircon/assert.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <lib/devmgr-launcher/processargs.h>

namespace {

constexpr const char* kDevmgrPath = "/boot/bin/devmgr";

} // namespace

namespace devmgr_launcher {

zx_status_t Launch(const char* driver_search_path, const char* sys_device_path,
                   zx::job* devmgr_job, zx::channel* devfs_root) {
    zx::job job, job_copy;
    zx_status_t status = zx::job::create(*zx::job::default_job(), 0, &job);
    if (status != ZX_OK) {
        return status;
    }
    status = job.duplicate(ZX_RIGHT_SAME_RIGHTS, &job_copy);
    if (status != ZX_OK) {
        return status;
    }

    launchpad_t* lp;
    launchpad_create_with_jobs(job.get(), job_copy.release(), "test-devmgr", &lp);
    launchpad_load_from_file(lp, kDevmgrPath);
    launchpad_clone(lp, LP_CLONE_FDIO_STDIO);

    int argc = 1;
    const char* argv[5] = {
        kDevmgrPath,
    };
    if (driver_search_path != nullptr) {
        argv[argc++] = "--driver-search-path";
        argv[argc++] = driver_search_path;
    }
    if (sys_device_path != nullptr) {
        argv[argc++] = "--sys-device-driver";
        argv[argc++] = sys_device_path;
    }
    ZX_DEBUG_ASSERT(static_cast<size_t>(argc) <= fbl::count_of(argv));
    launchpad_set_args(lp, argc, argv);

    const char* nametable[1] = { };
    uint32_t count = 0;

    // Pass /boot to the new devmgr
    {
        zx::channel client, server;
        status = zx::channel::create(0, &client, &server);
        if (status != ZX_OK) {
            return status;
        }

        fdio_ns_t* ns;
        status = fdio_ns_get_installed(&ns);
        if (status != ZX_OK) {
            return status;
        }

        status = fdio_ns_connect(ns, "/boot", ZX_FS_RIGHT_READABLE, server.release());
        if (status != ZX_OK) {
            return status;
        }
        launchpad_add_handle(lp, client.release(), PA_HND(PA_NS_DIR, count));
        nametable[count++] = "/boot";
    }

    ZX_DEBUG_ASSERT(count <= fbl::count_of(nametable));
    launchpad_set_nametable(lp, count, nametable);

    zx::channel devfs, devfs_server;
    status = zx::channel::create(0, &devfs, &devfs_server);
    if (status != ZX_OK) {
        return status;
    }
    launchpad_add_handle(lp, devfs_server.release(), DEVMGR_LAUNCHER_DEVFS_ROOT_HND);

    const char* errmsg;
    status = launchpad_go(lp, nullptr, &errmsg);
    if (status != ZX_OK) {
        return status;
    }

    *devmgr_job = std::move(job);
    *devfs_root = std::move(devfs);
    return ZX_OK;
}

} // namespace devmgr_integration_test
