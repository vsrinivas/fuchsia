// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>

#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <fdio/io.h>
#include <fdio/namespace.h>
#include <fdio/util.h>

#include <stdlib.h>
#include <unistd.h>

static zx_status_t add_fdio(launchpad_t* lp,
                            zx_handle_t handles[FDIO_MAX_HANDLES],
                            uint32_t types[FDIO_MAX_HANDLES],
                            zx_status_t status) {
    if (status == ZX_ERR_BAD_HANDLE)
        return ZX_OK;
    if (status == ZX_ERR_NOT_SUPPORTED)
        return ZX_OK;
    if (status > 0) {
        return launchpad_add_handles(lp, status, handles, types);
    } else {
        launchpad_abort(lp, status, "add_fdio: failed");
        return status;
    }
}

zx_status_t launchpad_clone(launchpad_t* lp, uint32_t what) {
    zx_handle_t handles[FDIO_MAX_HANDLES];
    uint32_t types[FDIO_MAX_HANDLES];
    zx_status_t status;

    if (what & LP_CLONE_FDIO_NAMESPACE) {
        fdio_flat_namespace_t* flat;
        status = fdio_ns_export_root(&flat);
        if (status == ZX_OK) {
            launchpad_set_nametable(lp, flat->count, flat->path);
            launchpad_add_handles(lp, flat->count, flat->handle, flat->type);
            free(flat);
        } else if (status != ZX_ERR_NOT_FOUND) {
            launchpad_abort(lp, status, "clone: error cloning namespace");
            return status;
        }
    }
    if (what & LP_CLONE_FDIO_STDIO) {
        for (int fd = 0; fd < 3; fd++) {
            add_fdio(lp, handles, types, fdio_clone_fd(fd, fd, handles, types));
        }
    }
    if (what & LP_CLONE_ENVIRON) {
        launchpad_set_environ(lp, (const char* const*)environ);
    }
    if (what & LP_CLONE_DEFAULT_JOB) {
        zx_handle_t job;
        if (zx_handle_duplicate(zx_job_default(), ZX_RIGHT_SAME_RIGHTS, &job) == ZX_OK) {
            launchpad_add_handle(lp, job, PA_HND(PA_JOB_DEFAULT, 0));
        }
    }
    return launchpad_get_status(lp);
}

zx_status_t launchpad_clone_fd(launchpad_t* lp, int fd, int target_fd) {
    zx_handle_t handles[FDIO_MAX_HANDLES];
    uint32_t types[FDIO_MAX_HANDLES];
    return add_fdio(lp, handles, types,
                    fdio_clone_fd(fd, target_fd, handles, types));
}

zx_status_t launchpad_transfer_fd(launchpad_t* lp, int fd, int target_fd) {
    zx_handle_t handles[FDIO_MAX_HANDLES];
    uint32_t types[FDIO_MAX_HANDLES];
    return add_fdio(lp, handles, types,
                    fdio_transfer_fd(fd, target_fd, handles, types));
}
