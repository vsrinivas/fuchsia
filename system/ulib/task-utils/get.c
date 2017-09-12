// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <task-utils/get.h>

#include <zircon/syscalls.h>
#include <task-utils/walker.h>

typedef struct {
    zx_koid_t desired_koid;
    zx_handle_t found_handle;
    zx_obj_type_t found_type;
} get_task_ctx_t;

static zx_status_t common_callback(zx_obj_type_t type, get_task_ctx_t* ctx,
                                   zx_handle_t handle, zx_koid_t koid) {
    if (koid == ctx->desired_koid) {
        zx_handle_t dup;
        zx_status_t s = zx_handle_duplicate(handle, ZX_RIGHT_SAME_RIGHTS, &dup);
        if (s != ZX_OK) {
            return s;
        }
        ctx->found_handle = dup;
        ctx->found_type = type;
        return ZX_ERR_STOP;
    }
    return ZX_OK;
}

static zx_status_t job_callback(void* ctx, int depth, zx_handle_t handle,
                                zx_koid_t koid, zx_koid_t parent_koid) {
    return common_callback(ZX_OBJ_TYPE_JOB, ctx, handle, koid);
}

static zx_status_t process_callback(void* ctx, int depth, zx_handle_t handle,
                                    zx_koid_t koid, zx_koid_t parent_koid) {
    return common_callback(ZX_OBJ_TYPE_PROCESS, ctx, handle, koid);
}

static zx_status_t thread_callback(void* ctx, int depth, zx_handle_t handle,
                                   zx_koid_t koid, zx_koid_t parent_koid) {
    return common_callback(ZX_OBJ_TYPE_THREAD, ctx, handle, koid);
}

zx_status_t get_task_by_koid(zx_koid_t koid,
                             zx_obj_type_t* type, zx_handle_t* out) {
    if (type == NULL || out == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }
    get_task_ctx_t ctx = {
        .desired_koid = koid,
        .found_handle = ZX_HANDLE_INVALID,
        .found_type = ZX_OBJ_TYPE_NONE,
    };
    zx_status_t s = walk_root_job_tree(
        job_callback, process_callback, thread_callback, &ctx);
    if (s == ZX_ERR_STOP) {
        *type = ctx.found_type;
        *out = ctx.found_handle;
        return ZX_OK;
    }
    if (s == ZX_OK) {
        // The callback would have returned ZX_ERR_STOP if it found anything.
        return ZX_ERR_NOT_FOUND;
    }
    return s;
}
