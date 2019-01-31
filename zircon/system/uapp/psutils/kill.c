// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <zircon/syscalls.h>
#include <task-utils/walker.h>

static zx_status_t callback(void* ctx, int depth, zx_handle_t handle,
                            zx_koid_t koid, zx_koid_t parent_koid) {
    zx_koid_t task_id = *(zx_koid_t*)ctx;
    if (koid == task_id) {
        zx_task_kill(handle);
        // found and killed the task - abort the search
        return ZX_ERR_INTERNAL;
    }
    return ZX_OK;
}

int main(int argc, const char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <task id>...\n", argv[0]);
        return -1;
    }

    bool errored = false;

    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if ((arg[0] == 'p' || arg[1] == 'j') && (arg[1] == ':')) {
            // Skip leading "p:" or "j:".
            arg += 2;
        }
        char* endptr;
        zx_koid_t task_id = strtoll(arg, &endptr, 10);
        if (*endptr != '\0') {
            fprintf(stderr, "\"%s\" is not a valid task id\n", arg);
            errored = true;
            continue;
        }
        zx_status_t status = walk_root_job_tree(callback, callback, NULL,
                                                &task_id);
        if (status == ZX_OK) {
            fprintf(stderr, "task %lu not found\n", task_id);
            errored = true;
            continue;
        }
    }
    return errored ? -1 : 0;
}
