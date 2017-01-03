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

static mx_status_t launch(int argc, const char** argv, mx_handle_t h) {
    mx_status_t status;
    mx_handle_t handles[4];
    uint32_t ids[4];

    if ((status = mxio_clone_root(handles, ids)) < 0) {
        fprintf(stderr, "fs_mount: Could not clone mxio root\n");
        return status;
    }
    if ((handles[1] = mx_log_create(0)) < 0) {
        fprintf(stderr, "fs_mount: Could not create log\n");
        mx_handle_close(handles[0]);
        return handles[1];
    }
    if ((handles[2] = mx_log_create(0)) < 0) {
        fprintf(stderr, "fs_mount: Could not create secondary log\n");
        mx_handle_close(handles[0]);
        mx_handle_close(handles[1]);
        return handles[2];
    }
    handles[3] = h;
    ids[1] = MX_HND_INFO(MX_HND_TYPE_MXIO_LOGGER, 1);
    ids[2] = MX_HND_INFO(MX_HND_TYPE_MXIO_LOGGER, 2);
    ids[3] = MX_HND_INFO(MX_HND_TYPE_USER0, 0);

    mx_handle_t proc;
    if ((proc = launchpad_launch_mxio_etc(argv[0], argc, argv,
                                          (const char* const*) environ,
                                          sizeof(handles) / sizeof(handles[0]),
                                          handles, ids)) <= 0) {
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
    if ((fd = open(devicepath, O_RDONLY)) < 0) {
        fprintf(stderr, "Error opening block device\n");
        return -1;
    }
    disk_format_t df = detect_disk_format(fd);
    close(fd);

    return mount(devicepath, mountpath, df, &options, launch);
}
