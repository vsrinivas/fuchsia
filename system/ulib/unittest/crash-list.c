// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-list.h"

#include <stdlib.h>
#include <threads.h>

#include <magenta/listnode.h>
#include <magenta/status.h>
#include <magenta/syscalls/object.h>
#include <unittest/unittest.h>

// Details of process or thread registered as expected to crash.
typedef struct crash_proc {
    mx_handle_t handle;
    mx_koid_t koid;
    // Node stored in crash handler list.
    list_node_t node;
} crash_proc_t;

struct crash_list {
    // The list may be accessed by the main test thread and crash handler thread.
    mtx_t mutex;
    // Head of list containing crash_proc_t.
    list_node_t should_crash_procs;
};

crash_list_t crash_list_new(void) {
    crash_list_t crash_list = malloc(sizeof(struct crash_list));
    if (crash_list == NULL) {
        UNITTEST_TRACEF("FATAL: could not malloc crash list\n");
        exit(MX_ERR_INTERNAL);
    }
    int ret = mtx_init(&crash_list->mutex, mtx_plain);
    if (ret != thrd_success) {
        UNITTEST_TRACEF("FATAL: could not create crash list mutex : error %s\n",
            mx_status_get_string(ret));
        exit(MX_ERR_INTERNAL);
    }
    crash_list->should_crash_procs =
        (list_node_t) LIST_INITIAL_VALUE(crash_list->should_crash_procs);
    return crash_list;
}

void crash_list_register(crash_list_t crash_list, mx_handle_t handle) {
    if (crash_list == NULL) {
        UNITTEST_TRACEF("FATAL: crash list was NULL, run test with RUN_TEST_ENABLE_CRASH_HANDLER\n");
        exit(MX_ERR_INTERNAL);
    }
    mx_info_handle_basic_t info;
    mx_status_t status = mx_object_get_info(handle, MX_INFO_HANDLE_BASIC,
                                            &info, sizeof(info), NULL, NULL);
    if (status != MX_OK) {
        UNITTEST_TRACEF("FATAL: could not get handle info : error %s\n",
            mx_status_get_string(status));
        exit(MX_ERR_INTERNAL);
    }
    mx_handle_t copy;
    status = mx_handle_duplicate(handle, MX_RIGHT_SAME_RIGHTS, &copy);
    if (status != MX_OK) {
        UNITTEST_TRACEF("FATAL: could not duplicate handle : error %s\n",
            mx_status_get_string(status));
        exit(MX_ERR_INTERNAL);
    }
    crash_proc_t* crash_proc = malloc(sizeof(crash_proc_t));
    if (crash_list == NULL) {
        UNITTEST_TRACEF("FATAL: could not malloc crash proc\n");
        exit(MX_ERR_INTERNAL);
    }
    crash_proc->handle = copy;
    crash_proc->koid = info.koid;

    mtx_lock(&crash_list->mutex);
    list_add_head(&crash_list->should_crash_procs, &crash_proc->node);
    mtx_unlock(&crash_list->mutex);
}

mx_handle_t crash_list_delete_koid(crash_list_t crash_list,
                                   mx_koid_t koid) {
    if (crash_list == NULL) {
        UNITTEST_TRACEF("FATAL: crash list was NULL, run test with RUN_TEST_ENABLE_CRASH_HANDLER\n");
        exit(MX_ERR_INTERNAL);
    }
    mx_handle_t deleted_proc = MX_HANDLE_INVALID;
    crash_proc_t* cur = NULL;
    crash_proc_t* tmp = NULL;

    mtx_lock(&crash_list->mutex);
    list_for_every_entry_safe(&crash_list->should_crash_procs, cur, tmp, crash_proc_t, node) {
        if (cur->koid == koid) {
            deleted_proc = cur->handle;
            list_delete(&cur->node);
            free(cur);
            break;
        }
    }
    mtx_unlock(&crash_list->mutex);
    return deleted_proc;
}

bool crash_list_delete(crash_list_t crash_list) {
    if (crash_list == NULL) {
        UNITTEST_TRACEF("FATAL: crash list was NULL, run test with RUN_TEST_ENABLE_CRASH_HANDLER\n");
        exit(MX_ERR_INTERNAL);
    }
    crash_proc_t* cur = NULL;
    crash_proc_t* tmp = NULL;

    bool deleted = false;
    list_for_every_entry_safe(&crash_list->should_crash_procs, cur, tmp, crash_proc_t, node) {
        mx_handle_close(cur->handle);
        deleted = true;
        list_delete(&cur->node);
        free(cur);
    }
    mtx_destroy(&crash_list->mutex);
    free(crash_list);
    return deleted;
}
