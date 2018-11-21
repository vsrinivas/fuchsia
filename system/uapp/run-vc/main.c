// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/device/manager/c/fidl.h>
#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/util.h>
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

int main(int argc, const char** argv) {
    int fd;
    int retry = 30;

    while ((fd = open("/dev/misc/dmctl", O_RDWR)) < 0) {
        if (--retry == 0) {
            fprintf(stderr, "run-vc: could not connect to virtual console\n");
            return -1;
        }
    }
    zx_handle_t dmctl;
    zx_status_t status = fdio_get_service_handle(fd, &dmctl);
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

    uint32_t types[FDIO_MAX_HANDLES];
    zx_handle_t handles[FDIO_MAX_HANDLES];
    uint32_t dcount, hcount;
    if (zx_channel_read(h0, 0, types, handles,
                        sizeof(types), FDIO_MAX_HANDLES, &dcount, &hcount) < 0) {
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

    fdio_spawn_action_t actions[1 + FDIO_MAX_HANDLES] = {
        {.action = FDIO_SPAWN_ACTION_SET_NAME, .name = {.data = pname}},
    };
    for (uint32_t i = 0; i < hcount; i++) {
        actions[1 + i].action = FDIO_SPAWN_ACTION_ADD_HANDLE;
        actions[1 + i].h.id = types[i];
        actions[1 + i].h.handle = handles[i];
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
