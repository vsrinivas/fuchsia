// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <magenta/syscalls.h>

#include "devcoordinator.h"

static mxio_dispatcher_t* coordinator_dispatcher;
static mx_handle_t devhost_job_handle;

mx_status_t coordinator_handler(mx_handle_t h, void* cb, void* cookie) {
    return ERR_NOT_SUPPORTED;
}

void coordinator_init(mx_handle_t root_job) {
    printf("coordinator_init()\n");

    mx_status_t status = mx_job_create(root_job, 0u, &devhost_job_handle);
    if (status < 0) {
        printf("unable to create devhost job\n");
    }

    mxio_dispatcher_create(&coordinator_dispatcher, coordinator_handler);
}

void coordinator(void) {
    printf("coordinator()\n");
    mxio_dispatcher_run(coordinator_dispatcher);
}