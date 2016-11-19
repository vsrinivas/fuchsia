// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <magenta/device/devmgr.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/util.h>

#include "mount.h"

mx_status_t mount_remote_handle(const char* where, mx_handle_t* h) {
    int fd;
    if ((fd = open(where, O_DIRECTORY | O_RDWR)) < 0) {
        return ERR_BAD_STATE;
    }
    xprintf("fs_mount: mount_remote_handle at: %s\n", where);
    mx_status_t status = NO_ERROR;
    if (ioctl_devmgr_mount_fs(fd, h) != sizeof(mx_handle_t)) {
        fprintf(stderr, "fs_mount: Could not mount remote handle on %s\n", where);
        status = ERR_BAD_STATE;
    }
    xprintf("fs_mount: mount_remote_handle completed without error\n");
    close(fd);
    return status;
}

int launch(int argc, const char** argv, mx_handle_t h) {
    mx_handle_t handles[4];
    uint32_t ids[4];

    if (mxio_clone_root(handles, ids) < 0) {
        fprintf(stderr, "fs_mount: Could not clone mxio root\n");
        return -1;
    }
    if ((handles[1] = mx_log_create(0)) < 0) {
        fprintf(stderr, "fs_mount: Could not create log\n");
        mx_handle_close(handles[0]);
        return -1;
    }
    if ((handles[2] = mx_log_create(0)) < 0) {
        fprintf(stderr, "fs_mount: Could not create secondary log\n");
        mx_handle_close(handles[0]);
        mx_handle_close(handles[1]);
        return -1;
    }
    handles[3] = h;
    ids[1] = MX_HND_INFO(MX_HND_TYPE_MXIO_LOGGER, 1);
    ids[2] = MX_HND_INFO(MX_HND_TYPE_MXIO_LOGGER, 2);
    ids[3] = MX_HND_INFO(MX_HND_TYPE_USER0, 0);

    mx_handle_t proc;
    if ((proc = launchpad_launch_mxio_etc(argv[0], argc, argv,
                                          (const char* const*) environ,
                                          arraylen(handles), handles, ids)) <= 0) {
        fprintf(stderr, "fs_mount: cannot launch %s\n", argv[0]);
        return -1;
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

    mx_status_t status = mx_handle_wait_one(proc, MX_SIGNAL_SIGNALED,
                                            MX_TIME_INFINITE, NULL);
    if (status != NO_ERROR) {
        fprintf(stderr, "fs_mount: Error waiting for filesystem to terminate\n");
    }
    mx_handle_close(proc);
    return status;
}
