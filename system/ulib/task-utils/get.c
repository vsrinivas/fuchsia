// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <task-utils/get.h>

#include <magenta/syscalls.h>
#include <task-utils/walker.h>

typedef struct {
    mx_koid_t desired_koid;
    mx_handle_t found_handle;
    mx_obj_type_t found_type;
} get_task_ctx_t;

static mx_status_t common_callback(mx_obj_type_t type, get_task_ctx_t* ctx,
                                   mx_handle_t handle, mx_koid_t koid) {
    if (koid == ctx->desired_koid) {
        mx_handle_t dup;
        mx_status_t s = mx_handle_duplicate(handle, MX_RIGHT_SAME_RIGHTS, &dup);
        if (s != MX_OK) {
            return s;
        }
        ctx->found_handle = dup;
        ctx->found_type = type;
        return MX_ERR_STOP;
    }
    return MX_OK;
}

static mx_status_t job_callback(void* ctx, int depth, mx_handle_t handle,
                                mx_koid_t koid, mx_koid_t parent_koid) {
    return common_callback(MX_OBJ_TYPE_JOB, ctx, handle, koid);
}

static mx_status_t process_callback(void* ctx, int depth, mx_handle_t handle,
                                    mx_koid_t koid, mx_koid_t parent_koid) {
    return common_callback(MX_OBJ_TYPE_PROCESS, ctx, handle, koid);
}

static mx_status_t thread_callback(void* ctx, int depth, mx_handle_t handle,
                                   mx_koid_t koid, mx_koid_t parent_koid) {
    return common_callback(MX_OBJ_TYPE_THREAD, ctx, handle, koid);
}

mx_status_t get_task_by_koid(mx_koid_t koid,
                             mx_obj_type_t* type, mx_handle_t* out) {
    if (type == NULL || out == NULL) {
        return MX_ERR_INVALID_ARGS;
    }
    get_task_ctx_t ctx = {
        .desired_koid = koid,
        .found_handle = MX_HANDLE_INVALID,
        .found_type = MX_OBJ_TYPE_NONE,
    };
    mx_status_t s = walk_root_job_tree(
        job_callback, process_callback, thread_callback, &ctx);
    if (s == MX_ERR_STOP) {
        *type = ctx.found_type;
        *out = ctx.found_handle;
        return MX_OK;
    }
    if (s == MX_OK) {
        // The callback would have returned MX_ERR_STOP if it found anything.
        return MX_ERR_NOT_FOUND;
    }
    return s;
}
