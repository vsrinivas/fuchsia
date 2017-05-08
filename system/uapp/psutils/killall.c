// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <magenta/syscalls.h>
#include <task-utils/walker.h>

const char* kill_name;
int killed = 0;

mx_status_t process_callback(int depth, mx_handle_t process, mx_koid_t koid) {
    char name[MX_MAX_NAME_LEN];
    mx_status_t status = mx_object_get_property(process, MX_PROP_NAME, name, sizeof(name));
    if (status != NO_ERROR) {
      return status;
    }
    if (!strcmp(name, kill_name) || !strcmp(basename(name), kill_name)) {
      mx_task_kill(process);
      printf("Killed %" PRIu64 " %s\n", koid, name);
      killed++;
    }
    return NO_ERROR;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <process>\n", argv[0]);
        return -1;
    }

    kill_name = argv[1];

    walk_root_job_tree(NULL, process_callback, NULL);
    if (killed == 0) {
        fprintf(stderr, "no tasks found\n");
        return -1;
    }
}
