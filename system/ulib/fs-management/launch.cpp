// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fs-management/mount.h>
#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <zircon/compiler.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <fdio/io.h>
#include <fdio/util.h>


zx_status_t launch_logs_async(int argc, const char** argv, zx_handle_t* handles,
                              uint32_t* types, size_t len) {
    launchpad_t* lp;
    launchpad_create(0, argv[0], &lp);
    launchpad_clone(lp, LP_CLONE_ALL & (~LP_CLONE_FDIO_STDIO));
    launchpad_load_from_file(lp, argv[0]);
    launchpad_set_args(lp, argc, argv);

    for (size_t i = 0; i < len; i++) {
        launchpad_add_handle(lp, handles[i], types[i]);
    }
    zx_handle_t h;
    zx_log_create(0, &h);
    if (h != ZX_HANDLE_INVALID) {
        launchpad_add_handle(lp, h, PA_HND(PA_FDIO_LOGGER,
                                           0 | FDIO_FLAG_USE_FOR_STDIO));
    }

    zx_status_t status;
    const char* errmsg;
    if ((status = launchpad_go(lp, NULL, &errmsg)) != ZX_OK) {
        fprintf(stderr, "fs-management: Cannot launch %s: %d: %s\n", argv[0], status, errmsg);
    }
    return status;
}

static void init_stdio(launchpad_t** lp, int argc, const char** argv,
                       zx_handle_t* handles, uint32_t* types, size_t len) {
    launchpad_create(0, argv[0], lp);
    launchpad_clone(*lp, LP_CLONE_ALL);
    launchpad_load_from_file(*lp, argv[0]);
    launchpad_set_args(*lp, argc, argv);

    for (size_t i = 0; i < len; i++) {
        launchpad_add_handle(*lp, handles[i], types[i]);
    }
}

zx_status_t launch_stdio_sync(int argc, const char** argv, zx_handle_t* handles,
                              uint32_t* types, size_t len) {
    launchpad_t* lp;
    init_stdio(&lp, argc, argv, handles, types, len);

    zx_status_t status;
    zx_handle_t proc;
    const char* errmsg;
    if ((status = launchpad_go(lp, &proc, &errmsg)) != ZX_OK) {
        fprintf(stderr, "fs-management: Cannot launch %s: %d: %s\n", argv[0], status, errmsg);
        return status;
    }

    status = zx_object_wait_one(proc, ZX_PROCESS_TERMINATED, ZX_TIME_INFINITE, NULL);
    if (status != ZX_OK) {
        fprintf(stderr, "launch: Error waiting for process to terminate\n");
        zx_handle_close(proc);
        return status;
    }

    zx_info_process_t info;
    if ((status = zx_object_get_info(proc, ZX_INFO_PROCESS, &info, sizeof(info), NULL, NULL)) < 0) {
        fprintf(stderr, "launch: Failed to get process info\n");
        zx_handle_close(proc);
        return status;
    }
    zx_handle_close(proc);
    if (!info.exited || info.return_code != 0) {
        return ZX_ERR_BAD_STATE;
    }
    return ZX_OK;
}

zx_status_t launch_stdio_async(int argc, const char** argv, zx_handle_t* handles,
                               uint32_t* types, size_t len) {
    launchpad_t* lp;
    init_stdio(&lp, argc, argv, handles, types, len);

    zx_status_t status;
    const char* errmsg;
    if ((status = launchpad_go(lp, NULL, &errmsg)) != ZX_OK) {
        fprintf(stderr, "fs-management: Cannot launch %s: %d: %s\n", argv[0], status, errmsg);
        return status;
    }
    return status;
}
