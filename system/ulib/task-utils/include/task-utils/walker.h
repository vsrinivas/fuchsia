// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>

__BEGIN_CDECLS

// Called on a task (job/process/thread) node by walk_job_tree().
//
// context: The same value passed to walk_[root_]job_tree(), letting callers pass
//     a context (e.g., object or struct) into each callback of a walk.
// depth: The distance from root_job. root_job has depth 0, direct
//     children have depth 1, and so on.
// task: A handle to the job/process/thread. Will be closed automatically
//     after the callback returns, so callers should duplicate the handle
//     if they want to use it later.
// koid: The koid of the task that the handle points to.
// parent_koid: The koid of the parent task (e.g., the process the contains the
//     thread, the job that contains the process, or the job that contains the
//     job).
//
// If the callback returns a value other than MX_OK, the job tree walk will
// terminate without visiting any other node, and the mx_status_t value will be
// returned by walk_job_tree().
typedef mx_status_t(task_callback_t)(
    void* context, int depth,
    mx_handle_t task, mx_koid_t koid, mx_koid_t parent_koid);

// Walks the job/process/task tree rooted in root_job. Visits tasks in
// depth-first pre order. Any callback arg may be NULL.
// |context| is passed to all callbacks.
mx_status_t walk_job_tree(mx_handle_t root_job,
                          task_callback_t job_callback,
                          task_callback_t process_callback,
                          task_callback_t thread_callback,
                          void* context);

// Calls walk_job_tree() on the system's root job. Will fail if the calling
// process does not have the rights to access the root job.
// TODO(dbort): Add a different lib/API to get the system root job and remove
// this function.
mx_status_t walk_root_job_tree(task_callback_t job_callback,
                               task_callback_t process_callback,
                               task_callback_t thread_callback,
                               void* context);

__END_CDECLS

// C++ interface
#ifdef __cplusplus
// Interface for walking a job tree.
class TaskEnumerator {
public:
    // Each of these methods visits the corresponding task type. If any On*()
    // method returns a value other than MX_OK, the enumeration stops. See
    // |task_callback_t| for a description of parameters.
    virtual mx_status_t OnJob(
        int depth, mx_handle_t job, mx_koid_t koid, mx_koid_t parent_koid) {
        return MX_ERR_NOT_SUPPORTED;
    }
    virtual mx_status_t OnProcess(
        int depth, mx_handle_t process, mx_koid_t koid, mx_koid_t parent_koid) {
        return MX_ERR_NOT_SUPPORTED;
    }
    virtual mx_status_t OnThread(
        int depth, mx_handle_t process, mx_koid_t koid, mx_koid_t parent_koid) {
        return MX_ERR_NOT_SUPPORTED;
    }

    // Walks the job/process/task tree rooted in root_job. Visits tasks in
    // depth-first pre order.
    mx_status_t WalkJobTree(mx_handle_t root_job);

    // Calls WalkJobTree() on the system's root job. Will fail if the calling
    // process does not have the rights to access the root job.
    // TODO(dbort): Add a different lib/API to get the system root job and
    // remove this function.
    mx_status_t WalkRootJobTree();

protected:
    TaskEnumerator() = default;
    virtual ~TaskEnumerator() = default;

    // Subclasses must overload these to indicate which task types to actually
    // visit. Avoids, e.g., visiting every thread in the system when a caller
    // only cares about jobs.
    virtual bool has_on_job() const { return false; }
    virtual bool has_on_process() const { return false; }
    virtual bool has_on_thread() const { return false; }
};
#endif // __cplusplus
