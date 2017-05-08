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
    task_callback_t job_callback, task_callback_t process_callback,
    task_callback_t thread_callback,
    mx_handle_t job, mx_koid_t job_koid, int depth);

static koid_table_t* make_koid_table(void) {
    koid_table_t* table = malloc(sizeof(*table));
    table->num_entries = 0;
    table->capacity = kNumInitialKoids;
    table->entries = malloc(table->capacity * sizeof(table->entries[0]));
    return table;
}

static size_t koid_table_byte_capacity(koid_table_t* table) {
    return table->capacity * sizeof(table->entries[0]);
}

static void realloc_koid_table(koid_table_t* table, size_t new_capacity) {
    table->entries = realloc(table->entries, new_capacity * sizeof(table->entries[0]));
    table->capacity = new_capacity;
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
        if (status != NO_ERROR) {
            fprintf(stderr, "ERROR: mx_object_get_info(%" PRIu64 ", %s, ...) failed: %s (%d)\n",
                    parent_koid, kind_name, mx_status_get_string(status), status);
            return status;
        }
        if (actual == avail) {
            break;
        }
    }

    // if we're still too small at least warn the user
    if (actual < avail) {
        fprintf(stderr, "WARNING: mx_object_get_info(%" PRIu64 ", %s, ...) truncated %zu/%zu results\n",
                parent_koid, kind_name, avail - actual, avail);
    }

    koids->num_entries = actual;
    return NO_ERROR;
}

static mx_status_t do_threads_worker(
    koid_table_t* koids, task_callback_t thread_callback,
    mx_handle_t process, mx_koid_t process_koid, int depth) {

    mx_status_t status;

    // get the list of processes under this job
    status = fetch_children(process, process_koid, MX_INFO_PROCESS_THREADS, "MX_INFO_PROCESS_THREADS",
                            koids);
    if (status != NO_ERROR) {
        return status;
    }

    for (size_t n = 0; n < koids->num_entries; n++) {
        mx_handle_t child;
        status = mx_object_get_child(process, koids->entries[n], MX_RIGHT_SAME_RIGHTS, &child);
        if (status == NO_ERROR) {
            // call the thread_callback if supplied
            if (thread_callback) {
                status = (thread_callback)(depth, child, koids->entries[n]);
                // abort on failure
                if (status != NO_ERROR) {
                    return status;
                }
            }

            mx_handle_close(child);
        } else {
            fprintf(stderr, "WARNING: mx_object_get_child(%" PRIu64 ", (proc)%" PRIu64 ", ...) failed: %s (%d)\n",
                    process_koid, koids->entries[n], mx_status_get_string(status), status);
        }
    }

    return NO_ERROR;
}

static mx_status_t do_threads(
    task_callback_t thread_callback,
    mx_handle_t job, mx_koid_t job_koid, int depth) {

    koid_table_t* koids = make_koid_table();
    mx_status_t status = do_threads_worker(koids, thread_callback,
                                           job, job_koid, depth);
    free_koid_table(koids);
    return status;
}

static mx_status_t do_processes_worker(
    koid_table_t* koids,
    task_callback_t process_callback, task_callback_t thread_callback,
    mx_handle_t job, mx_koid_t job_koid, int depth) {

    mx_status_t status;

    // get the list of processes under this job
    status = fetch_children(job, job_koid, MX_INFO_JOB_PROCESSES, "MX_INFO_JOB_PROCESSES",
                            koids);
    if (status != NO_ERROR) {
        return status;
    }

    for (size_t n = 0; n < koids->num_entries; n++) {
        mx_handle_t child;
        status = mx_object_get_child(job, koids->entries[n], MX_RIGHT_SAME_RIGHTS, &child);
        if (status == NO_ERROR) {
            // call the process_callback if supplied
            if (process_callback) {
                status = (process_callback)(depth, child, koids->entries[n]);
                // abort on failure
                if (status != NO_ERROR) {
                    return status;
                }
            }

            if (thread_callback) {
                status = do_threads(thread_callback, child, koids->entries[n], depth + 1);
                // abort on failure
                if (status != NO_ERROR) {
                    return status;
                }
            }

            mx_handle_close(child);
        } else {
            fprintf(stderr, "WARNING: mx_object_get_child(%" PRIu64 ", (proc)%" PRIu64 ", ...) failed: %s (%d)\n",
                    job_koid, koids->entries[n], mx_status_get_string(status), status);
        }
    }

    return NO_ERROR;
}

static mx_status_t do_processes(
    task_callback_t process_callback, task_callback_t thread_callback,
    mx_handle_t job, mx_koid_t job_koid, int depth) {

    koid_table_t* koids = make_koid_table();
    mx_status_t status = do_processes_worker(koids, process_callback, thread_callback,
                                             job, job_koid, depth);
    free_koid_table(koids);
    return status;
}

static mx_status_t do_jobs_worker(
    koid_table_t* koids,
    task_callback_t job_callback,
    task_callback_t process_callback,
    task_callback_t thread_callback,
    mx_handle_t job, mx_koid_t job_koid, int depth) {

    mx_status_t status;

    // get a list of child jobs for this job
    status = fetch_children(job, job_koid, MX_INFO_JOB_CHILDREN, "MX_INFO_JOB_CHILDREN",
                            koids);
    if (status != NO_ERROR) {
        return status;
    }

    // drill down into the job tree
    for (size_t n = 0; n < koids->num_entries; n++) {
        mx_handle_t child;
        status = mx_object_get_child(job, koids->entries[n], MX_RIGHT_SAME_RIGHTS, &child);
        if (status == NO_ERROR) {
            // call the job_callback if supplied
            if (job_callback) {
                status = (job_callback)(depth, child, koids->entries[n]);
                // abort on failure
                if (status != NO_ERROR) {
                    return status;
                }
            }

            // recurse to its children
            status = walk_job_tree_internal(
                job_callback, process_callback, thread_callback,
                child, koids->entries[n], depth + 1);
            // abort on failure
            if (status != NO_ERROR) {
                return status;
            }

            mx_handle_close(child);
        } else {
            fprintf(stderr,
                    "WARNING: mx_object_get_child(%" PRIu64 ", (job)%" PRIu64
                    ", ...) failed: %s (%d)\n",
                    job_koid, koids->entries[n], mx_status_get_string(status), status);
        }
    }

    return NO_ERROR;
}

static mx_status_t do_jobs(
    task_callback_t job_callback,
    task_callback_t process_callback,
    task_callback_t thread_callback,
    mx_handle_t job, mx_koid_t job_koid, int depth) {

    koid_table_t* koids = make_koid_table();
    mx_status_t status = do_jobs_worker(koids, job_callback, process_callback,
                                        thread_callback, job, job_koid, depth);
    free_koid_table(koids);
    return status;
}

static mx_status_t walk_job_tree_internal(
    task_callback_t job_callback, task_callback_t process_callback,
    task_callback_t thread_callback,
    mx_handle_t job, mx_koid_t job_koid, int depth) {

    if (process_callback != NULL || thread_callback != NULL) {
        mx_status_t status = do_processes(
            process_callback, thread_callback, job, job_koid, depth);
        if (status != NO_ERROR) {
            return status;
        }
    }

    return do_jobs(job_callback, process_callback, thread_callback,
                   job, job_koid, depth);
}

mx_status_t walk_job_tree(mx_handle_t root_job,
                          task_callback_t job_callback,
                          task_callback_t process_callback,
                          task_callback_t thread_callback) {
    mx_koid_t root_job_koid = 0;
    mx_info_handle_basic_t info;
    mx_status_t status = mx_object_get_info(root_job, MX_INFO_HANDLE_BASIC,
                                            &info, sizeof(info), NULL, NULL);
    if (status == NO_ERROR) {
        root_job_koid = info.koid;
    }
    // Else keep going with a koid of zero.

    if (job_callback) {
        status = (job_callback)(/* depth */ 0, root_job, root_job_koid);
        if (status != NO_ERROR) {
            return status;
        }
    }
    return walk_job_tree_internal(
        job_callback, process_callback, thread_callback,
        root_job, root_job_koid, /* depth */ 1);
}

mx_status_t walk_root_job_tree(task_callback_t job_callback,
                               task_callback_t process_callback,
                               task_callback_t thread_callback) {
    int fd = open("/dev/misc/sysinfo", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "task-utils/walker: cannot open sysinfo: %d\n", errno);
        return ERR_NOT_FOUND;
    }

    mx_handle_t root_job;
    size_t n = ioctl_sysinfo_get_root_job(fd, &root_job);
    close(fd);
    if (n != sizeof(root_job)) {
        fprintf(stderr, "task-utils/walker: cannot obtain root job\n");
        return ERR_NOT_FOUND;
    }

    mx_status_t s = walk_job_tree(
        root_job, job_callback, process_callback, thread_callback);
    mx_handle_close(root_job);
    return s;
}
