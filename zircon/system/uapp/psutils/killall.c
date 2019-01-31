// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fnmatch.h>
#include <inttypes.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zircon/syscalls.h>
#include <task-utils/walker.h>

const char* kill_name;
int killed = 0;

zx_status_t process_callback(void* unused_ctx, int depth, zx_handle_t process,
                             zx_koid_t koid, zx_koid_t parent_koid) {
    char name[ZX_MAX_NAME_LEN];
    zx_status_t status =
        zx_object_get_property(process, ZX_PROP_NAME, name, sizeof(name));
    if (status != ZX_OK) {
        return status;
    }
    if (!strcmp(name, kill_name) ||
        !fnmatch(kill_name, name, 0) ||
        !strcmp(basename(name), kill_name)) {
        zx_task_kill(process);
        printf("Killed %" PRIu64 " %s\n", koid, name);
        killed++;
    }
    return ZX_OK;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <process>\n", argv[0]);
        fprintf(stderr, "  <process> can be the name of a process, the basename of a process\n");
        fprintf(stderr, "  or glob pattern matching a process name.\n");
        return -1;
    }

    kill_name = argv[1];

    walk_root_job_tree(NULL, process_callback, NULL, NULL);
    if (killed == 0) {
        fprintf(stderr, "no tasks found\n");
        return -1;
    }
}
