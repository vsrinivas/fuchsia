// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <task-utils/get.h>

#include <magenta/syscalls.h>
#include <task-utils/walker.h>

mx_koid_t desired_koid;
mx_handle_t found_handle;
mx_obj_type_t found_type;

static mx_status_t common_callback(mx_obj_type_t type, mx_handle_t handle,
                                   mx_koid_t koid) {
    if (koid == desired_koid) {
        mx_handle_t dup;
        mx_status_t s = mx_handle_duplicate(handle, MX_RIGHT_SAME_RIGHTS, &dup);
        if (s != MX_OK) {
            return s;
        }
        found_handle = dup;
        found_type = type;
        return MX_ERR_STOP;
    }
    return MX_OK;
}

static mx_status_t job_callback(int depth, mx_handle_t handle,
                                mx_koid_t koid, mx_koid_t parent_koid) {
    return common_callback(MX_OBJ_TYPE_JOB, handle, koid);
}

static mx_status_t process_callback(int depth, mx_handle_t handle,
                                    mx_koid_t koid, mx_koid_t parent_koid) {
    return common_callback(MX_OBJ_TYPE_PROCESS, handle, koid);
}

static mx_status_t thread_callback(int depth, mx_handle_t handle,
                                   mx_koid_t koid, mx_koid_t parent_koid) {
    return common_callback(MX_OBJ_TYPE_THREAD, handle, koid);
}

mx_status_t get_task_by_koid(mx_koid_t koid,
                             mx_obj_type_t* type, mx_handle_t* out) {
    if (type == NULL || out == NULL) {
        return MX_ERR_INVALID_ARGS;
    }
    desired_koid = koid;
    found_handle = MX_HANDLE_INVALID;
    found_type = MX_OBJ_TYPE_NONE;
    mx_status_t s = walk_root_job_tree(
        job_callback, process_callback, thread_callback);
    desired_koid = 0;
    if (s == MX_ERR_STOP) {
        *type = found_type;
        *out = found_handle;
        found_handle = MX_HANDLE_INVALID;
        return MX_OK;
    }
    if (s == MX_OK) {
        // The callback would have returned MX_ERR_STOP if it found anything.
        return MX_ERR_NOT_FOUND;
    }
    return s;
}
