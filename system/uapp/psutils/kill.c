// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <magenta/syscalls.h>
#include <task-utils/walker.h>

mx_koid_t task_id;

mx_status_t callback(int depth, mx_handle_t handle, mx_koid_t koid) {
    if (koid == task_id) {
        mx_task_kill(handle);
        // found and killed the task - abort the search
        return ERR_INTERNAL;
    }
    return NO_ERROR;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <task id>\n", argv[0]);
        return -1;
    }

    task_id = atoll(argv[1]);

    mx_status_t status = walk_root_job_tree(callback, callback, NULL);
    if (status == NO_ERROR) {
        fprintf(stderr, "no task found\n");
        return -1;
    }
}
