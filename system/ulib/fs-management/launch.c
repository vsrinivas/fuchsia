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

static mx_status_t init_logs(mx_handle_t* hnd, uint32_t* ids, size_t* n) {
    mx_status_t status;
    if ((status = mx_log_create(0, &hnd[*n])) != NO_ERROR) {
        fprintf(stderr, "init_logs: Could not create log\n");
        return status;
    }
    ids[*n] = MX_HND_INFO(MX_HND_TYPE_MXIO_LOGGER, 0 | MXIO_FLAG_USE_FOR_STDIO);
    *n = *n + 1;

    return NO_ERROR;
}

static mx_status_t init_stdio(mx_handle_t* hnd, uint32_t* ids, size_t* n) {
    mx_status_t status;
    if ((status = mxio_clone_fd(1, 1, hnd + *n, ids + *n)) < 0) {
        fprintf(stderr, "init_stdio: Could not clone stdout\n");
        return status;
    }
    *n = *n + 1;

    if ((status = mxio_clone_fd(2, 2, hnd + *n, ids + *n)) < 0) {
        fprintf(stderr, "init_stdio: Could not clone stderr\n");
        return status;
    }
    *n = *n + 1;
    return NO_ERROR;
}

mx_status_t launch_logs_sync(int argc, const char** argv, mx_handle_t* handles,
                             uint32_t* types, size_t len) {
    mx_status_t status;
    mx_handle_t hnd[8];
    uint32_t ids[8];

    size_t n = 0;
    if ((status = mxio_clone_root(hnd, ids)) < 0) {
        fprintf(stderr, "launch: Could not clone mxio root\n");
        goto fail;
    }
    n++;
    if ((status = init_logs(hnd, ids, &n)) < 0) {
        goto fail;
    }

    if (n + len > countof(hnd)) {
        fprintf(stderr, "launch: Too many handles\n");
        status = ERR_INVALID_ARGS;
        goto fail;
    }

    for (size_t i = 0; i < len; i++) {
        hnd[n] = handles[i];
        ids[n] = types[i];
        n++;
    }

    // Launchpad consumes 'hnd'
    mx_handle_t proc;
    if ((proc = launchpad_launch_mxio_etc(argv[0], argc, argv,
                                          (const char* const*) environ,
                                          n, hnd, ids)) <= 0) {
        fprintf(stderr, "launch: cannot launch %s\n", argv[0]);
        return proc;
    }

    status = mx_handle_wait_one(proc, MX_PROCESS_SIGNALED, MX_TIME_INFINITE, NULL);
    if (status != NO_ERROR) {
        fprintf(stderr, "launch: Error waiting for process to terminate\n");
    }
    mx_handle_close(proc);
    return status;
fail:
    for (size_t i = 0; i < n; i++) {
        mx_handle_close(hnd[i]);
    }
    return status;
}

mx_status_t launch_stdio_sync(int argc, const char** argv, mx_handle_t* handles,
                              uint32_t* types, size_t len) {
    mx_status_t status;
    mx_handle_t hnd[8];
    uint32_t ids[8];

    size_t n = 0;
    if ((status = mxio_clone_root(hnd, ids)) < 0) {
        fprintf(stderr, "launch: Could not clone mxio root\n");
        goto fail;
    }
    n++;
    if ((status = init_stdio(hnd, ids, &n)) < 0) {
        goto fail;
    }

    if (n + len > countof(hnd)) {
        fprintf(stderr, "launch: Too many handles\n");
        status = ERR_INVALID_ARGS;
        goto fail;
    }

    for (size_t i = 0; i < len; i++) {
        hnd[n] = handles[i];
        ids[n] = types[i];
        n++;
    }

    // Launchpad consumes 'hnd'
    mx_handle_t proc;
    if ((proc = launchpad_launch_mxio_etc(argv[0], argc, argv,
                                          (const char* const*) environ,
                                          n, hnd, ids)) <= 0) {
        fprintf(stderr, "launch: cannot launch %s\n", argv[0]);
        return proc;
    }

    status = mx_handle_wait_one(proc, MX_PROCESS_SIGNALED, MX_TIME_INFINITE, NULL);
    if (status != NO_ERROR) {
        fprintf(stderr, "launch: Error waiting for process to terminate\n");
    }
    mx_handle_close(proc);
    return status;
fail:
    for (size_t i = 0; i < n; i++) {
        mx_handle_close(hnd[i]);
    }
    return status;
}

mx_status_t launch_stdio_async(int argc, const char** argv, mx_handle_t* handles,
                               uint32_t* types, size_t len) {
    mx_status_t status;
    mx_handle_t hnd[8];
    uint32_t ids[8];

    size_t n = 0;
    if ((status = mxio_clone_root(hnd, ids)) < 0) {
        fprintf(stderr, "launch: Could not clone mxio root\n");
        goto fail;
    }
    n++;
    if ((status = init_stdio(hnd, ids, &n)) < 0) {
        goto fail;
    }

    if (n + len > countof(hnd)) {
        fprintf(stderr, "launch: Too many handles\n");
        status = ERR_INVALID_ARGS;
        goto fail;
    }

    for (size_t i = 0; i < len; i++) {
        hnd[n] = handles[i];
        ids[n] = types[i];
        n++;
    }

    // Launchpad consumes 'hnd'
    mx_handle_t proc;
    if ((proc = launchpad_launch_mxio_etc(argv[0], argc, argv,
                                          (const char* const*) environ,
                                          n, hnd, ids)) <= 0) {
        fprintf(stderr, "launch: cannot launch %s\n", argv[0]);
        return proc;
    }

    mx_handle_close(proc);
    return status;
fail:
    for (size_t i = 0; i < n; i++) {
        mx_handle_close(hnd[i]);
    }
    return status;
}
