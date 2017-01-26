// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "launch.h"
#include <launchpad/vmo.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/io.h>
#include <mxio/util.h>

#include <unistd.h>

static mx_status_t add_mxio(launchpad_t* lp,
                            mx_handle_t handles[MXIO_MAX_HANDLES],
                            uint32_t types[MXIO_MAX_HANDLES],
                            mx_status_t status) {
    if (status == ERR_BAD_HANDLE)
        return NO_ERROR;
    if (status == ERR_NOT_SUPPORTED)
        return NO_ERROR;
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

    if (what & LP_CLONE_MXIO_ROOT) {
        add_mxio(lp, handles, types, mxio_clone_root(handles, types));
    }
    if (what & LP_CLONE_MXIO_CWD) {
        add_mxio(lp, handles, types, mxio_clone_cwd(handles, types));
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
        if (mx_handle_duplicate(mx_job_default(), MX_RIGHT_SAME_RIGHTS, &job) == NO_ERROR) {
            launchpad_add_handle(lp, job, MX_HND_INFO(MX_HND_TYPE_JOB, 0));
        }
    }
    return launchpad_get_status(lp);
}

mx_status_t launchpad_clone_mxio_root(launchpad_t* lp) {
    return launchpad_clone(lp, LP_CLONE_MXIO_ROOT);
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

mx_status_t launchpad_clone_mxio_cwd(launchpad_t* lp) {
    return launchpad_clone(lp, LP_CLONE_MXIO_CWD);
}

mx_status_t launchpad_add_all_mxio(launchpad_t* lp) {
    launchpad_clone(lp, LP_CLONE_MXIO_ROOT | LP_CLONE_MXIO_CWD);
    for (int fd = 0; fd < MAX_MXIO_FD; ++fd) {
        launchpad_clone_fd(lp, fd, fd);
    }
    return launchpad_get_status(lp);
}

mx_handle_t launchpad_launch_mxio_vmo_etc(mx_handle_t job,
                                          const char* name, mx_handle_t vmo,
                                          int argc, const char* const* argv,
                                          const char* const* envp,
                                          size_t hnds_count,
                                          mx_handle_t* handles, uint32_t* ids) {
    launchpad_t* lp = NULL;

    if (name == NULL)
        name = argv[0];

    mx_handle_t transfer_job;
    mx_status_t status =
        mx_handle_duplicate(job, MX_RIGHT_SAME_RIGHTS, &transfer_job);
    if (status == NO_ERROR)
        status = launchpad_create_with_jobs(job, transfer_job, name, &lp);
    if (status == NO_ERROR)
        status = launchpad_elf_load(lp, vmo);
    if (status == NO_ERROR) {
        status = launchpad_load_vdso(lp, MX_HANDLE_INVALID);
    } else {
        mx_handle_close(vmo);
    }
    if (status == NO_ERROR)
        status = launchpad_add_vdso_vmo(lp);
    if (status == NO_ERROR)
        status = launchpad_arguments(lp, argc, argv);
    if (status == NO_ERROR)
        status = launchpad_environ(lp, envp);
    if (status == NO_ERROR)
        status = launchpad_add_all_mxio(lp);
    if (status == NO_ERROR)
        status = launchpad_add_handles(lp, hnds_count, handles, ids);

    return finish_launch(lp, status, handles, hnds_count);
}

mx_handle_t launchpad_launch_mxio_etc(const char* name, int argc,
                                      const char* const* argv,
                                      const char* const* envp,
                                      size_t hnds_count, mx_handle_t* handles,
                                      uint32_t* ids) {
    return launchpad_launch_mxio_vmo_etc(
        mx_job_default(), name, launchpad_vmo_from_file(argv[0]), argc, argv,
        envp, hnds_count, handles, ids);
}

mx_handle_t launchpad_launch_mxio(const char* name,
                                  int argc, const char* const* argv) {
    return launchpad_launch_mxio_etc(name, argc, argv,
                                     (const char* const*)environ,
                                     0, NULL, NULL);
}
