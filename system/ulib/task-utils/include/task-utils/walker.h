// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>

__BEGIN_CDECLS

// Called on a task (job/process/thread) node by walk_job_tree().
//
// depth: The distance from root_job. root_job has depth 0, direct
//     children have depth 1, and so on.
// task: A handle to the job/process/thread. Will be closed automatically
//     after the callback returns, so callers should duplicate the handle
//     if they want to use it later.
// koid: The koid of the task that the handle points to.
//
// If the callback returns a value other than NO_ERROR, the job tree walk will
// terminate without visiting any other node, and the mx_status_t value will be
// returned by walk_job_tree().
typedef mx_status_t(task_callback_t)(
    int depth, mx_handle_t task, mx_koid_t koid);

// Walks the job/process/task tree rooted in root_job. Visits tasks in
// depth-first pre order. Any callback arg may be NULL.
// TODO: Let callers provide a void* cookie, and pass it into the callbacks.
mx_status_t walk_job_tree(mx_handle_t root_job,
                          task_callback_t job_callback,
                          task_callback_t process_callback,
                          task_callback_t thread_callback);

// Calls walk_job_tree() on the system's root job. Will fail if the calling
// process does not have the rights to access the root job.
mx_status_t walk_root_job_tree(task_callback_t job_callback,
                               task_callback_t process_callback,
                               task_callback_t thread_callback);

__END_CDECLS
