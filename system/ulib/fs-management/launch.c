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
#include <magenta/compiler.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/io.h>
#include <mxio/util.h>


mx_status_t launch_logs_async(int argc, const char** argv, mx_handle_t* handles,
                              uint32_t* types, size_t len) {
    launchpad_t* lp;
    launchpad_create(0, argv[0], &lp);
    launchpad_clone(lp, LP_CLONE_ALL & (~LP_CLONE_MXIO_STDIO));
    launchpad_load_from_file(lp, argv[0]);
    launchpad_set_args(lp, argc, argv);

    for (size_t i = 0; i < len; i++) {
        launchpad_add_handle(lp, handles[i], types[i]);
    }
    mx_handle_t h;
    mx_log_create(0, &h);
    if (h != MX_HANDLE_INVALID) {
        launchpad_add_handle(lp, h, PA_HND(PA_MXIO_LOGGER,
                                           0 | MXIO_FLAG_USE_FOR_STDIO));
    }

    mx_status_t status;
    const char* errmsg;
    if ((status = launchpad_go(lp, NULL, &errmsg)) != NO_ERROR) {
        fprintf(stderr, "fs-management: Cannot launch %s: %d: %s\n", argv[0], status, errmsg);
    }
    return status;
}

static void init_stdio(launchpad_t** lp, int argc, const char** argv,
                       mx_handle_t* handles, uint32_t* types, size_t len) {
    launchpad_create(0, argv[0], lp);
    launchpad_clone(*lp, LP_CLONE_ALL);
    launchpad_load_from_file(*lp, argv[0]);
    launchpad_set_args(*lp, argc, argv);

    for (size_t i = 0; i < len; i++) {
        launchpad_add_handle(*lp, handles[i], types[i]);
    }
}

mx_status_t launch_stdio_sync(int argc, const char** argv, mx_handle_t* handles,
                              uint32_t* types, size_t len) {
    launchpad_t* lp;
    init_stdio(&lp, argc, argv, handles, types, len);

    mx_status_t status;
    mx_handle_t proc;
    const char* errmsg;
    if ((status = launchpad_go(lp, &proc, &errmsg)) != NO_ERROR) {
        fprintf(stderr, "fs-management: Cannot launch %s: %d: %s\n", argv[0], status, errmsg);
        return status;
    }

    status = mx_object_wait_one(proc, MX_PROCESS_TERMINATED, MX_TIME_INFINITE, NULL);
    if (status != NO_ERROR) {
        fprintf(stderr, "launch: Error waiting for process to terminate\n");
        mx_handle_close(proc);
        return status;
    }

    mx_info_process_t info;
    if ((status = mx_object_get_info(proc, MX_INFO_PROCESS, &info, sizeof(info), NULL, NULL)) < 0) {
        fprintf(stderr, "launch: Failed to get process info\n");
        mx_handle_close(proc);
        return status;
    }
    mx_handle_close(proc);
    if (!info.exited || info.return_code != 0) {
        return ERR_BAD_STATE;
    }
    return NO_ERROR;
}

mx_status_t launch_stdio_async(int argc, const char** argv, mx_handle_t* handles,
                               uint32_t* types, size_t len) {
    launchpad_t* lp;
    init_stdio(&lp, argc, argv, handles, types, len);

    mx_status_t status;
    const char* errmsg;
    if ((status = launchpad_go(lp, NULL, &errmsg)) != NO_ERROR) {
        fprintf(stderr, "fs-management: Cannot launch %s: %d: %s\n", argv[0], status, errmsg);
        return status;
    }
    return status;
}
