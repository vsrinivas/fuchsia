// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/device/manager/c/fidl.h>
#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/watcher.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/processargs.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

static zx_status_t dmctl_watch_func(int dirfd, int event, const char* fn, void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE) {
        return ZX_OK;
    }
    if (!strcmp(fn, "dmctl")) {
        return ZX_ERR_STOP;
    }
    return ZX_OK;
};

static zx_status_t open_dmctl(int* fd) {
    int dirfd = open("/dev/misc", O_RDONLY);
    if (dirfd < 0) {
        return ZX_ERR_IO;
    }
    zx_status_t status = fdio_watch_directory(dirfd, dmctl_watch_func, ZX_TIME_INFINITE, NULL);
    if (status != ZX_ERR_STOP) {
        if (status == ZX_OK) {
            status = ZX_ERR_BAD_STATE;
        }
        printf("failed to watch /dev/misc: %s\n", zx_status_get_string(status));
        close(dirfd);
        return status;
    }
    *fd = openat(dirfd, "dmctl", O_RDWR);
    close(dirfd);
    if (*fd < 0) {
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

int main(int argc, const char** argv) {
    int fd;
    zx_status_t status = open_dmctl(&fd);
    if (status != ZX_OK) {
        fprintf(stderr, "failed to open dmctl: %s\n", zx_status_get_string(status));
        return -1;
    }
    zx_handle_t dmctl;
    status = fdio_get_service_handle(fd, &dmctl);
    if (status != ZX_OK) {
        fprintf(stderr, "error %s converting fd to handle\n", zx_status_get_string(status));
        return -1;
    }

    zx_handle_t h0, h1;
    if (zx_channel_create(0, &h0, &h1) < 0) {
        return -1;
    }

    if (fuchsia_device_manager_ExternalControllerOpenVirtcon(dmctl, h1) < 0) {
        return -1;
    }
    h1 = ZX_HANDLE_INVALID;
    zx_handle_close(dmctl);

    zx_object_wait_one(h0, ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                       ZX_TIME_INFINITE, NULL);

    uint32_t type = 0;
    zx_handle_t handle = ZX_HANDLE_INVALID;
    uint32_t dcount = 0, hcount = 0;
    if (zx_channel_read(h0, 0, &type, &handle, sizeof(type), 1, &dcount, &hcount) != ZX_OK) {
        return -1;
    }
    if (dcount / sizeof(uint32_t) != hcount) {
        return -1;
    }
    zx_handle_close(h0);

    // start shell if no arguments
    if (argc == 1) {
        argv[0] = "/boot/bin/sh";
    } else {
        argv++;
    }

    const char* pname = strrchr(argv[0], '/');
    if (pname == NULL) {
        pname = argv[0];
    } else {
        pname++;
    }

    uint32_t flags = FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_STDIO;

    fdio_spawn_action_t actions[2] = {
        {.action = FDIO_SPAWN_ACTION_SET_NAME, .name = {.data = pname}},
        {.action = FDIO_SPAWN_ACTION_ADD_HANDLE, .h = {.id = type, .handle = handle}},
    };

    char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
    status = fdio_spawn_etc(ZX_HANDLE_INVALID, flags, argv[0], argv,
                            NULL, 1 + hcount, actions, NULL, err_msg);
    if (status != ZX_OK) {
        fprintf(stderr, "error %d (%s) launching: %s\n", status,
                zx_status_get_string(status), err_msg);
        return -1;
    }

    return 0;
}
