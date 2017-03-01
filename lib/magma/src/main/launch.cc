// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include <launchpad/launchpad.h>
#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <mxio/io.h>
#include <mxio/util.h>
#include <stdio.h>

bool launch(uint32_t argc, const char** argv)
{
    const char* env[] = {NULL};
    mx_handle_t vfs_root;
    uint32_t vfs_root_type;

    mx_status_t status = mxio_clone_root(&vfs_root, &vfs_root_type);
    if (status <= 0)
        return DRETF(false, "failed to clone root handle: %d", status);

    mx_handle_t job_copy = MX_HANDLE_INVALID;
    mx_handle_duplicate(mx_job_default(), MX_RIGHT_SAME_RIGHTS, &job_copy);

    launchpad_t* lp;
    launchpad_create(job_copy, argv[0], &lp);
    launchpad_load_from_file(lp, argv[0]);
    launchpad_set_args(lp, argc, argv);
    launchpad_set_environ(lp, env);
    launchpad_add_handle(lp, vfs_root, vfs_root_type);

    mx_handle_t logger;
    status = mx_log_create(0, &logger);
    if (status < 0)
        return DRETF(false, "cannot create debuglog handle: %d", status);

    launchpad_add_handle(lp, logger,
                         MX_HND_INFO(MX_HND_TYPE_MXIO_LOGGER, MXIO_FLAG_USE_FOR_STDIO | 0));

    const char* errmsg;
    status = launchpad_go(lp, NULL, &errmsg);
    if (status < 0)
        return DRETF(false, "launchpad %s failed: %s: %d", argv[0], errmsg, status);

    DLOG("launch %s OK", argv[0]);
    return true;
}
