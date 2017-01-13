// Copyright 2016 The Fuchsia Authors. All rights reserved.
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
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/util.h>

struct {
    const char* name;
    disk_format_t df;
} FILESYSTEMS[] = {
    {"minfs", DISK_FORMAT_MINFS},
    {"fat", DISK_FORMAT_FAT},
};

int usage(void) {
    fprintf(stderr, "usage: fsck [ <option>* ] devicepath filesystem\n");
    fprintf(stderr, " -v  : Verbose mode\n");
    fprintf(stderr, " values for 'filesystem' include:\n");
    for (size_t i = 0; i < sizeof(FILESYSTEMS) / sizeof(FILESYSTEMS[0]); i++) {
        fprintf(stderr, "  '%s'\n", FILESYSTEMS[i].name);
    }
    return -1;
}

int parse_args(int argc, char** argv, bool* verbose, disk_format_t* df, char** devicepath) {
    *verbose = false;
    *df = DISK_FORMAT_UNKNOWN;
    while (argc > 1) {
        if (!strcmp(argv[1], "-v")) {
            *verbose = true;
        } else {
            break;
        }
        argc--;
        argv++;
    }
    if (argc < 3) {
        return usage();
    }

    *devicepath = argv[1];
    for (size_t i = 0; i < sizeof(FILESYSTEMS) / sizeof(FILESYSTEMS[0]); i++) {
        if (!strcmp(FILESYSTEMS[i].name, argv[2])) {
            *df = FILESYSTEMS[i].df;
            break;
        }
    }
    if (*df == DISK_FORMAT_UNKNOWN) {
        fprintf(stderr, "fs_fsck: Cannot format a device with filesystem '%s'\n", argv[2]);
        return -1;
    }
    return 0;
}

static mx_status_t launch(int argc, const char** argv, mx_handle_t* handles, uint32_t* types,
                          size_t len) {
    mx_status_t status;
    mx_handle_t hnd[8];
    uint32_t ids[8];

    size_t n = 0;
    if ((status = mxio_clone_root(hnd, ids)) < 0) {
        fprintf(stderr, "fs_fsck: Could not clone mxio root\n");
        return status;
    }
    n++;

    if ((status = mxio_clone_fd(1, 1, hnd + n, ids + n)) < 0) {
        fprintf(stderr, "fs_fsck: Could not clone stdout\n");
        goto fail;
    }
    n++;

    if ((status = mxio_clone_fd(2, 2, hnd + n, ids + n)) < 0) {
        fprintf(stderr, "fs_fsck: Could not clone stderr\n");
        goto fail;
    }
    n++;

    if (n + len > sizeof(hnd)/sizeof(hnd[0])) {
        fprintf(stderr, "fs_fsck: Too many handles\n");
        goto fail;
    }

    for (size_t i = 0; i < len; i++) {
        hnd[n] = handles[i];
        ids[n] = types[i];
        n++;
    }

    mx_handle_t proc;
    if ((proc = launchpad_launch_mxio_etc(argv[0], argc, argv,
                                          (const char* const*) environ,
                                          n, hnd, ids)) <= 0) {
        fprintf(stderr, "fs_fsck: cannot launch %s\n", argv[0]);
        return proc;
    }

    status = mx_handle_wait_one(proc, MX_PROCESS_SIGNALED, MX_TIME_INFINITE, NULL);
    if (status != NO_ERROR) {
        fprintf(stderr, "fs_fsck: Error waiting for fsck to terminate\n");
    }
    mx_handle_close(proc);
    return status;
fail:
    for (size_t i = 0; i < n; i++) {
        mx_handle_close(handles[i]);
    }
    return status;
}

int main(int argc, char** argv) {
    bool verbose;
    char* devicepath;
    disk_format_t df;
    int r;
    if ((r = parse_args(argc, argv, &verbose, &df, &devicepath))) {
        return r;
    }

    if (verbose) {
        printf("fs_fsck: Formatting device [%s]\n", devicepath);
    }

    if ((r = fsck(devicepath, df, launch)) < 0) {
        fprintf(stderr, "fs_fsck: Failed to format device: %d\n", r);
    }
    return r;
}
