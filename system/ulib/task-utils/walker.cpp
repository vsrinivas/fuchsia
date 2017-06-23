// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <task-utils/walker.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <magenta/device/sysinfo.h>
#include <magenta/status.h>
#include <magenta/syscalls.h>

// Immutable state of a specific call to walk_job_tree, passed along
// to most helper functions.
typedef struct {
    task_callback_t* job_callback;
    task_callback_t* process_callback;
    task_callback_t* thread_callback;
    void* callback_context;
} walk_ctx_t;

// A dynamically-managed array of koids.
// TODO(dbort): Turn into a class now that this is a .cpp file.
typedef struct {
    mx_koid_t* entries;
    size_t num_entries;
    size_t capacity; // allocation size
} koid_table_t;

// best first guess at number of children
static const size_t kNumInitialKoids = 128;

// when reallocating koid buffer because we were too small add this much extra
// on top of what the kernel says is currently needed
static const size_t kNumExtraKoids = 10;

static mx_status_t walk_job_tree_internal(
    const walk_ctx_t* ctx, mx_handle_t job, mx_koid_t job_koid, int depth);

static size_t koid_table_byte_capacity(koid_table_t* table) {
    return table->capacity * sizeof(table->entries[0]);
}

static void realloc_koid_table(koid_table_t* table, size_t new_capacity) {
    table->entries = reinterpret_cast<mx_koid_t*>(
        realloc(table->entries, new_capacity * sizeof(table->entries[0])));
    table->capacity = new_capacity;
}

static koid_table_t* make_koid_table(void) {
    koid_table_t* table =
        reinterpret_cast<koid_table_t*>(malloc(sizeof(*table)));
    table->num_entries = 0;
    table->entries = nullptr;
    realloc_koid_table(table, kNumInitialKoids);
    return table;
}

static void free_koid_table(koid_table_t* table) {
    free(table->entries);
    free(table);
}

static mx_status_t fetch_children(mx_handle_t parent, mx_koid_t parent_koid,
                                  int children_kind, const char* kind_name,
                                  koid_table_t* koids) {

    size_t actual = 0;
    size_t avail = 0;
    mx_status_t status;

    // this is inherently racy, but we retry once with a bit of slop to try to
    // get a complete list
    for (int pass = 0; pass < 2; ++pass) {
        if (actual < avail) {
            realloc_koid_table(koids, avail + kNumExtraKoids);
        }
        status = mx_object_get_info(parent, children_kind,
                                    koids->entries,
                                    koid_table_byte_capacity(koids),
                                    &actual, &avail);
        if (status != MX_OK) {
            fprintf(stderr,
                    "ERROR: mx_object_get_info(%" PRIu64 ", %s, ...) "
                    "failed: %s (%d)\n",
                    parent_koid, kind_name,
                    mx_status_get_string(status), status);
            return status;
        }
        if (actual == avail) {
            break;
        }
    }

    // if we're still too small at least warn the user
    if (actual < avail) {
        fprintf(stderr,
                "WARNING: mx_object_get_info(%" PRIu64 ", %s, ...) "
                "truncated %zu/%zu results\n",
                parent_koid, kind_name, avail - actual, avail);
    }

    koids->num_entries = actual;
    return MX_OK;
}

static mx_status_t do_threads_worker(
    const walk_ctx_t* ctx, koid_table_t* koids,
    mx_handle_t process, mx_koid_t process_koid, int depth) {

    mx_status_t status;

    // get the list of processes under this job
    status = fetch_children(process, process_koid, MX_INFO_PROCESS_THREADS,
                            "MX_INFO_PROCESS_THREADS", koids);
    if (status != MX_OK) {
        return status;
    }

    for (size_t n = 0; n < koids->num_entries; n++) {
        mx_handle_t child;
        status = mx_object_get_child(process, koids->entries[n],
                                     MX_RIGHT_SAME_RIGHTS, &child);
        if (status == MX_OK) {
            // call the thread_callback if supplied
            if (ctx->thread_callback) {
                status = (ctx->thread_callback)(
                    ctx->callback_context,
                    depth, child, koids->entries[n], process_koid);
                // abort on failure
                if (status != MX_OK) {
                    return status;
                }
            }

            mx_handle_close(child);
        } else {
            fprintf(stderr,
                    "WARNING: mx_object_get_child(%" PRIu64 ", "
                    "(proc)%" PRIu64 ", ...) failed: %s (%d)\n",
                    process_koid, koids->entries[n],
                    mx_status_get_string(status), status);
        }
    }

    return MX_OK;
}

static mx_status_t do_threads(
    const walk_ctx_t* ctx, mx_handle_t job, mx_koid_t job_koid, int depth) {

    koid_table_t* koids = make_koid_table();
    mx_status_t status = do_threads_worker(ctx, koids, job, job_koid, depth);
    free_koid_table(koids);
    return status;
}

static mx_status_t do_processes_worker(
    const walk_ctx_t* ctx, koid_table_t* koids,
    mx_handle_t job, mx_koid_t job_koid, int depth) {

    mx_status_t status;

    // get the list of processes under this job
    status = fetch_children(job, job_koid, MX_INFO_JOB_PROCESSES,
                            "MX_INFO_JOB_PROCESSES", koids);
    if (status != MX_OK) {
        return status;
    }

    for (size_t n = 0; n < koids->num_entries; n++) {
        mx_handle_t child;
        status = mx_object_get_child(job, koids->entries[n],
                                     MX_RIGHT_SAME_RIGHTS, &child);
        if (status == MX_OK) {
            // call the process_callback if supplied
            if (ctx->process_callback) {
                status = (ctx->process_callback)(ctx->callback_context,
                                                 depth, child,
                                                 koids->entries[n], job_koid);
                // abort on failure
                if (status != MX_OK) {
                    return status;
                }
            }

            if (ctx->thread_callback) {
                status = do_threads(ctx, child, koids->entries[n], depth + 1);
                // abort on failure
                if (status != MX_OK) {
                    return status;
                }
            }

            mx_handle_close(child);
        } else {
            fprintf(stderr,
                    "WARNING: mx_object_get_child(%" PRIu64 ", "
                    "(proc)%" PRIu64 ", ...) failed: %s (%d)\n",
                    job_koid, koids->entries[n],
                    mx_status_get_string(status), status);
        }
    }

    return MX_OK;
}

static mx_status_t do_processes(
    const walk_ctx_t* ctx, mx_handle_t job, mx_koid_t job_koid, int depth) {

    koid_table_t* koids = make_koid_table();
    mx_status_t status = do_processes_worker(ctx, koids, job, job_koid, depth);
    free_koid_table(koids);
    return status;
}

static mx_status_t do_jobs_worker(
    const walk_ctx_t* ctx, koid_table_t* koids,
    mx_handle_t job, mx_koid_t job_koid, int depth) {

    mx_status_t status;

    // get a list of child jobs for this job
    status = fetch_children(job, job_koid, MX_INFO_JOB_CHILDREN,
                            "MX_INFO_JOB_CHILDREN", koids);
    if (status != MX_OK) {
        return status;
    }

    // drill down into the job tree
    for (size_t n = 0; n < koids->num_entries; n++) {
        mx_handle_t child;
        status = mx_object_get_child(job, koids->entries[n],
                                     MX_RIGHT_SAME_RIGHTS, &child);
        if (status == MX_OK) {
            // call the job_callback if supplied
            if (ctx->job_callback) {
                status = (ctx->job_callback)(ctx->callback_context,
                                             depth, child,
                                             koids->entries[n], job_koid);
                // abort on failure
                if (status != MX_OK) {
                    return status;
                }
            }

            // recurse to its children
            status = walk_job_tree_internal(
                ctx, child, koids->entries[n], depth + 1);
            // abort on failure
            if (status != MX_OK) {
                return status;
            }

            mx_handle_close(child);
        } else {
            fprintf(stderr,
                    "WARNING: mx_object_get_child(%" PRIu64 ", (job)%" PRIu64
                    ", ...) failed: %s (%d)\n",
                    job_koid, koids->entries[n],
                    mx_status_get_string(status), status);
        }
    }

    return MX_OK;
}

static mx_status_t do_jobs(
    const walk_ctx_t* ctx, mx_handle_t job, mx_koid_t job_koid, int depth) {

    koid_table_t* koids = make_koid_table();
    mx_status_t status = do_jobs_worker(ctx, koids, job, job_koid, depth);
    free_koid_table(koids);
    return status;
}

static mx_status_t walk_job_tree_internal(
    const walk_ctx_t* ctx, mx_handle_t job, mx_koid_t job_koid, int depth) {

    if (ctx->process_callback != nullptr || ctx->thread_callback != nullptr) {
        mx_status_t status = do_processes(ctx, job, job_koid, depth);
        if (status != MX_OK) {
            return status;
        }
    }

    return do_jobs(ctx, job, job_koid, depth);
}

mx_status_t walk_job_tree(mx_handle_t root_job,
                          task_callback_t job_callback,
                          task_callback_t process_callback,
                          task_callback_t thread_callback,
                          void* context) {
    mx_koid_t root_job_koid = 0;
    mx_info_handle_basic_t info;
    mx_status_t status = mx_object_get_info(
        root_job, MX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
    if (status == MX_OK) {
        root_job_koid = info.koid;
    }
    // Else keep going with a koid of zero.

    if (job_callback) {
        status =
            (job_callback)(context, /* depth */ 0, root_job, root_job_koid, 0);
        if (status != MX_OK) {
            return status;
        }
    }
    walk_ctx_t ctx = {
        .job_callback = job_callback,
        .process_callback = process_callback,
        .thread_callback = thread_callback,
        .callback_context = context,
    };
    return walk_job_tree_internal(
        &ctx, root_job, root_job_koid, /* depth */ 1);
}

mx_status_t walk_root_job_tree(task_callback_t job_callback,
                               task_callback_t process_callback,
                               task_callback_t thread_callback,
                               void* context) {
    int fd = open("/dev/misc/sysinfo", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "task-utils/walker: cannot open sysinfo: %d\n", errno);
        return MX_ERR_NOT_FOUND;
    }

    mx_handle_t root_job;
    size_t n = ioctl_sysinfo_get_root_job(fd, &root_job);
    close(fd);
    if (n != sizeof(root_job)) {
        fprintf(stderr, "task-utils/walker: cannot obtain root job\n");
        return MX_ERR_NOT_FOUND;
    }

    mx_status_t s = walk_job_tree(
        root_job, job_callback, process_callback, thread_callback, context);
    mx_handle_close(root_job);
    return s;
}

// C++ interface

namespace {
static mx_status_t job_cpp_cb(void* ctx, int depth, mx_handle_t handle,
                              mx_koid_t koid, mx_koid_t parent_koid) {
    return reinterpret_cast<TaskEnumerator*>(ctx)->OnJob(
        depth, handle, koid, parent_koid);
}

static mx_status_t process_cpp_cb(void* ctx, int depth, mx_handle_t handle,
                                  mx_koid_t koid, mx_koid_t parent_koid) {
    return reinterpret_cast<TaskEnumerator*>(ctx)->OnProcess(
        depth, handle, koid, parent_koid);
}

static mx_status_t thread_cpp_cb(void* ctx, int depth, mx_handle_t handle,
                                 mx_koid_t koid, mx_koid_t parent_koid) {
    return reinterpret_cast<TaskEnumerator*>(ctx)->OnThread(
        depth, handle, koid, parent_koid);
}
} // namespace

mx_status_t TaskEnumerator::WalkJobTree(mx_handle_t root_job) {
    return walk_job_tree(root_job,
                         has_on_job() ? job_cpp_cb : nullptr,
                         has_on_process() ? process_cpp_cb : nullptr,
                         has_on_thread() ? thread_cpp_cb : nullptr,
                         reinterpret_cast<void*>(this));
}

mx_status_t TaskEnumerator::WalkRootJobTree() {
    return walk_root_job_tree(has_on_job() ? job_cpp_cb : nullptr,
                              has_on_process() ? process_cpp_cb : nullptr,
                              has_on_thread() ? thread_cpp_cb : nullptr,
                              reinterpret_cast<void*>(this));
}
