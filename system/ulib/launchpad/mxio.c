// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "launch.h"
#include <launchpad/vmo.h>

#include <magenta/syscalls.h>
#include <mxio/io.h>
#include <mxio/util.h>

#include <unistd.h>

static mx_status_t add_mxio(launchpad_t* lp,
                            mx_handle_t handles[MXIO_MAX_HANDLES],
                            uint32_t types[MXIO_MAX_HANDLES],
                            mx_status_t status) {
    if (status > 0) {
        size_t n = status;
        status = launchpad_add_handles(lp, n, handles, types);
        if (status != NO_ERROR) {
            for (size_t i = 0; i < n; ++i)
                mx_handle_close(handles[i]);
        }
    }
    return status;
}

mx_status_t launchpad_clone_mxio_root(launchpad_t* lp) {
    mx_handle_t handles[MXIO_MAX_HANDLES];
    uint32_t types[MXIO_MAX_HANDLES];
    return add_mxio(lp, handles, types, mxio_clone_root(handles, types));
}

mx_status_t launchpad_clone_fd(launchpad_t* lp, int fd, int target_fd) {
    mx_handle_t handles[MXIO_MAX_HANDLES];
    uint32_t types[MXIO_MAX_HANDLES];
    return add_mxio(lp, handles, types,
                    mxio_clone_fd(fd, target_fd, handles, types));
}

mx_status_t launchpad_add_all_mxio(launchpad_t* lp) {
    mx_status_t status = launchpad_clone_mxio_root(lp);
    if(status == NO_ERROR) {
      status = launchpad_clone_mxio_cwd(lp);
    }
    for (int fd = 0; status == NO_ERROR && fd < MAX_MXIO_FD; ++fd) {
        status = launchpad_clone_fd(lp, fd, fd);
        if (status == ERR_BAD_HANDLE)
            status = NO_ERROR;
        if (status == ERR_NOT_SUPPORTED)
            status = NO_ERROR;
    }
    return status;
}

mx_handle_t launchpad_launch_mxio_etc(const char* name,
                                      int argc, const char* const* argv,
                                      const char* const* envp,
                                      size_t hnds_count, mx_handle_t* handles,
                                      uint32_t* ids) {
    launchpad_t* lp;

    const char* filename = argv[0];
    if (name == NULL)
        name = filename;

    mx_status_t status = launchpad_create(name, &lp);
    if (status == NO_ERROR) {
        status = launchpad_elf_load(lp, launchpad_vmo_from_file(filename));
        if (status == NO_ERROR)
            status = launchpad_load_vdso(lp, MX_HANDLE_INVALID);
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
    }

    return finish_launch(lp, status, handles, hnds_count);
}

mx_handle_t launchpad_launch_mxio(const char* name,
                                  int argc, const char* const* argv) {
    return launchpad_launch_mxio_etc(name, argc, argv,
                                     (const char* const*)environ,
                                     0, NULL, NULL);
}
