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


int usage(void) {
    fprintf(stderr, "usage: mount [ <option>* ] devicepath mountpath\n");
    fprintf(stderr, " -v  : Verbose mode\n");
    fprintf(stderr, " -r  : Open the filesystem as read-only\n");
    return -1;
}

int parse_args(int argc, char** argv, mount_options_t* options,
               char** devicepath, char** mountpath) {
    while (argc > 1) {
        if (!strcmp(argv[1], "-v")) {
            options->verbose_mount = true;
        } else if (!strcmp(argv[1], "-r")) {
            options->readonly = true;
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
    *mountpath = argv[2];
    return 0;
}

static mx_status_t launch(int argc, const char** argv, mx_handle_t* handles, uint32_t* types, size_t len) {
    mx_status_t status;
    mx_handle_t hnd[8];
    uint32_t ids[8];

    size_t n = 0;
    if ((status = mxio_clone_root(hnd, ids)) < 0) {
        fprintf(stderr, "fs_mount: Could not clone mxio root\n");
        return status;
    }
    n++;

    if ((status = mx_log_create(0, &hnd[n])) < 0) {
        fprintf(stderr, "fs_mount: Could not create log\n");
        goto fail;
    }
    ids[n++] = MX_HND_INFO(MX_HND_TYPE_MXIO_LOGGER, 1);

    if ((status = mx_log_create(0, &hnd[n])) < 0) {
        fprintf(stderr, "fs_mount: Could not create secondary log\n");
        goto fail;
    }
    ids[n++] = MX_HND_INFO(MX_HND_TYPE_MXIO_LOGGER, 2);

    if (n + len > sizeof(hnd)/sizeof(hnd[0])) {
        fprintf(stderr, "fs_mount: Too many handles\n");
        goto fail;
    }

    for (size_t i = 0; i < len; i++) {
        hnd[n] = handles[i];
        ids[n] = types[i];
        n++;
    }

    mx_handle_t proc;
    // Launchpad consumes 'hnd'; if we fail after this, then we can assume those handles are closed.
    if ((proc = launchpad_launch_mxio_etc(argv[0], argc, argv,
                                          (const char* const*) environ,
                                          n, hnd, ids)) <= 0) {
        fprintf(stderr, "fs_mount: cannot launch %s\n", argv[0]);
        return proc;
    }

    // TODO(smklein): There is currently a race condition that exists within
    // "launchpad_launch". If a parent process "A" launches a child process "B",
    // the parent process is also responsible for acting like a loader service
    // to the child process. Therefore, if process "A" launches "B", but
    // terminates before it finishes loading "B", then "B" can crash
    // unexpectedly. To avoid this problem, 'mount' should be executed as a
    // background process from mxsh. When mount can launch filesystem servers
    // and delegate the responsibilities of the loader service elsewhere, it can
    // terminate without waiting for the child filesystem to terminate as well.

    status = mx_handle_wait_one(proc, MX_PROCESS_SIGNALED, MX_TIME_INFINITE,
                                NULL);
    if (status != NO_ERROR) {
        fprintf(stderr, "fs_mount: Error waiting for filesystem to terminate\n");
    }
    mx_handle_close(proc);
    return status;

fail:
    for (size_t i = 0; i < n; i++) {
        mx_handle_close(hnd[i]);
    }
    return status;
}

int main(int argc, char** argv) {
    char* devicepath;
    char* mountpath;
    mount_options_t options;
    memcpy(&options, &default_mount_options, sizeof(mount_options_t));
    int r;
    if ((r = parse_args(argc, argv, &options, &devicepath, &mountpath))) {
        return r;
    }

    if (options.verbose_mount) {
        printf("fs_mount: Mounting device [%s] on path [%s]\n", devicepath, mountpath);
    }

    int fd;
    if ((fd = open(devicepath, O_RDWR)) < 0) {
        fprintf(stderr, "Error opening block device\n");
        return -1;
    }
    disk_format_t df = detect_disk_format(fd);
    return mount(fd, mountpath, df, &options, launch);
}
