// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magenta_platform_launcher.h"

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

namespace magma {

bool MagentaPlatformLauncher::Launch(mx_handle_t job, const char* name, int argc,
                                     const char* const* argv, const char* envp[],
                                     mx_handle_t* handles, uint32_t* types, size_t hcount)
{
    mx_handle_t job_copy = MX_HANDLE_INVALID;
    mx_handle_duplicate(job, MX_RIGHT_SAME_RIGHTS, &job_copy);

    mx_handle_t vfs_root;
    uint32_t vfs_root_type;

    mx_status_t status = mxio_clone_root(&vfs_root, &vfs_root_type);
    if (status <= 0)
        return DRETF(false, "failed to clone root handle: %d", status);

    launchpad_t* lp;
    launchpad_create(job_copy, name, &lp);
    launchpad_load_from_file(lp, argv[0]);
    launchpad_set_args(lp, argc, argv);
    launchpad_set_environ(lp, envp);

    status = launchpad_add_handle(lp, vfs_root, vfs_root_type);
    if (status != NO_ERROR)
        return DRETF(false, "launchpad_add_handle failed: %d", status);

    mx_handle_t h;
    status = mx_log_create(0, &h);
    if (status < 0)
        return DRETF(false, "mx_log_create failed: %d", status);

    launchpad_add_handle(lp, h, MX_HND_INFO(MX_HND_TYPE_MXIO_LOGGER, MXIO_FLAG_USE_FOR_STDIO | 0));
    launchpad_add_handles(lp, hcount, handles, types);

    const char* errmsg;
    status = launchpad_go(lp, NULL, &errmsg);
    if (status != NO_ERROR)
        return DRETF(false, "launchpad %s (%s) failed: %s: %d\n", argv[0], name, errmsg, status);

    magma::log(magma::LOG_INFO, "launch %s (%s) OK", argv[0], name);

    return true;
}

} // namespace