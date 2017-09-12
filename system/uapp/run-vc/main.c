// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <limits.h>
#include <zircon/device/dmctl.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <fdio/io.h>
#include <fdio/util.h>
#include <fdio/watcher.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char** argv) {
    int fd;
    int retry = 30;

    while ((fd = open("/dev/misc/dmctl", O_RDWR)) < 0) {
        if (--retry == 0) {
            fprintf(stderr, "run-vc: could not connect to virtual console\n");
            return -1;
        }
    }

    zx_handle_t h0, h1;
    if (zx_channel_create(0, &h0, &h1) < 0) {
        return -1;
    }
    if (ioctl_dmctl_open_virtcon(fd, &h1) < 0) {
        return -1;
    }
    close(fd);

    zx_object_wait_one(h0, ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                       ZX_TIME_INFINITE, NULL);

    uint32_t types[2];
    zx_handle_t handles[2];
    uint32_t dcount, hcount;
    if (zx_channel_read(h0, 0, types, handles, sizeof(types), 2, &dcount, &hcount) < 0) {
        return -1;
    }
    if ((dcount != sizeof(types)) || (hcount != 2)) {
        return -1;
    }
    zx_handle_close(h0);

    // start shell if no arguments
    if (argc == 1) {
        argv[0] = (char*) "/boot/bin/sh";
    } else {
        argv++;
        argc--;
    }

    char* pname = strrchr(argv[0], '/');
    if (pname == NULL) {
        pname = argv[0];
    } else {
        pname++;
    }

    launchpad_t* lp;
    launchpad_create(0, pname, &lp);
    launchpad_clone(lp, LP_CLONE_FDIO_NAMESPACE | LP_CLONE_ENVIRON);
    launchpad_add_handles(lp, 2, handles, types);
    launchpad_set_args(lp, argc, (const char* const*) argv);
    launchpad_load_from_file(lp, argv[0]);

    zx_status_t status;
    const char* errmsg;
    if ((status = launchpad_go(lp, NULL, &errmsg)) < 0) {
        fprintf(stderr, "error %d launching: %s\n", status, errmsg);
        return -1;
    }

    return 0;
}

