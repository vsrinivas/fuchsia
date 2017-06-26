// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>

#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/io.h>
#include <mxio/namespace.h>
#include <mxio/util.h>

#include <stdlib.h>
#include <unistd.h>

static mx_status_t add_mxio(launchpad_t* lp,
                            mx_handle_t handles[MXIO_MAX_HANDLES],
                            uint32_t types[MXIO_MAX_HANDLES],
                            mx_status_t status) {
    if (status == MX_ERR_BAD_HANDLE)
        return MX_OK;
    if (status == MX_ERR_NOT_SUPPORTED)
        return MX_OK;
    if (status > 0) {
        return launchpad_add_handles(lp, status, handles, types);
    } else {
        launchpad_abort(lp, status, "add_mxio: failed");
        return status;
    }
}

mx_status_t launchpad_clone(launchpad_t* lp, uint32_t what) {
    mx_handle_t handles[MXIO_MAX_HANDLES];
    uint32_t types[MXIO_MAX_HANDLES];
    mx_status_t status;

    if (what & LP_CLONE_MXIO_NAMESPACE) {
        mxio_flat_namespace_t* flat;
        status = mxio_ns_export_root(&flat);
        if (status == MX_OK) {
            launchpad_set_nametable(lp, flat->count, flat->path);
            launchpad_add_handles(lp, flat->count, flat->handle, flat->type);
            free(flat);
        } else if (status != MX_ERR_NOT_FOUND) {
            launchpad_abort(lp, status, "clone: error cloning namespace");
            return status;
        }
    }
    if (what & LP_CLONE_MXIO_CWD && (status = mxio_clone_cwd(handles, types)) > 0) {
        add_mxio(lp, handles, types, status);
    }
    if (what & LP_CLONE_MXIO_STDIO) {
        for (int fd = 0; fd < 3; fd++) {
            add_mxio(lp, handles, types, mxio_clone_fd(fd, fd, handles, types));
        }
    }
    if (what & LP_CLONE_ENVIRON) {
        launchpad_set_environ(lp, (const char* const*)environ);
    }
    if (what & LP_CLONE_DEFAULT_JOB) {
        mx_handle_t job;
        if (mx_handle_duplicate(mx_job_default(), MX_RIGHT_SAME_RIGHTS, &job) == MX_OK) {
            launchpad_add_handle(lp, job, PA_HND(PA_JOB_DEFAULT, 0));
        }
    }
    return launchpad_get_status(lp);
}

mx_status_t launchpad_clone_fd(launchpad_t* lp, int fd, int target_fd) {
    mx_handle_t handles[MXIO_MAX_HANDLES];
    uint32_t types[MXIO_MAX_HANDLES];
    return add_mxio(lp, handles, types,
                    mxio_clone_fd(fd, target_fd, handles, types));
}

mx_status_t launchpad_transfer_fd(launchpad_t* lp, int fd, int target_fd) {
    mx_handle_t handles[MXIO_MAX_HANDLES];
    uint32_t types[MXIO_MAX_HANDLES];
    return add_mxio(lp, handles, types,
                    mxio_transfer_fd(fd, target_fd, handles, types));
}
