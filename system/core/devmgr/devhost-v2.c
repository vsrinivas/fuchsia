// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/log.h>

#include <mxio/util.h>

#include "devcoordinator.h"



static void devhost_io_init(void) {
    mx_handle_t h;
    if (mx_log_create(MX_LOG_FLAG_DEVICE, &h) < 0) {
        return;
    }
    mxio_t* logger;
    if ((logger = mxio_logger_create(h)) == NULL) {
        return;
    }
    close(1);
    mxio_bind_to_fd(logger, 1, 0);
    dup2(1, 2);
}

int main(int argc, char** argv) {
    devhost_io_init();

    printf("devhost-v2: main()\n");

    mx_handle_t hrpc = mx_get_startup_handle(MX_HND_INFO(MX_HND_TYPE_USER0, 0));
    if (hrpc == MX_HANDLE_INVALID) {
        printf("devhost: rpc handle invalid\n");
        return -1;
    }

    return 0;
}