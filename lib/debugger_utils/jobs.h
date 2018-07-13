// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_DEBUGGER_UTILS_JOBS_H_
#define GARNET_LIB_DEBUGGER_UTILS_JOBS_H_

#include <functional>

#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>

namespace debugger_utils {

zx::job GetDefaultJob();

using JobTreeJobCallback = std::function<zx_status_t(
    zx::job& job, zx_koid_t koid, zx_koid_t parent_koid, int depth)>;

using JobTreeProcessCallback = std::function<zx_status_t(
    zx::process& process, zx_koid_t koid, zx_koid_t parent_koid, int depth)>;

using JobTreeThreadCallback = std::function<zx_status_t(
    zx::thread& thread, zx_koid_t koid, zx_koid_t parent_koid, int depth)>;

// Walk the job tree of |job|, beginning at |job| and descending to all its
// children. Jobs are searched in depth-first order.
// The initial |job| argument of WalkJobTree() is not consumed, however a
// callback may consume it (see below).
//
// A reference to the task object (job,process,thread) is passed to each
// callback. If the callback wishes to take over ownership of the handle it
// can do so with task.release(), with one caveat for job and process handles:
// Even though the handle is now owned by the callback, it is loaned to
// WalkJobTree, and the callback must not close/replace/transfer/whatever the
// handle until WalkJobTree returns.
// All other handles obtained during the walk are automagically closed by the
// time WalkJobTree() returns.
//
// The walk continues until either of the following happens:
// 1) Any callback returns something other than ZX_OK.
//    By convention if tree walking has successfully completed and you want to
//    "early exit" then return ZX_ERR_STOP from a callback.
// 2) |job_callback| has taken ownership of the |zx::job| object.
//    Processes of the job and their threads are still walked, but no further
//    jobs are walked, and ZX_ERR_STOP is returned.
//    Remember the job handle must remain open until WalkJobTree() returns.
//
// TODO(dje): Maybe allow a callback to take ownership of a job handle and
// still continue walking of further jobs (maintaining the restriction that
// the caller can't close the handle until WalkJobTree() returns).

zx_status_t WalkJobTree(zx::job& job, JobTreeJobCallback* job_callback,
                        JobTreeProcessCallback* process_callback,
                        JobTreeThreadCallback* thread_callback);

// Simple wrapper on WalkJobTree for finding processes.
// Returns zx::process(ZX_HANDLE_INVALID) if not found.
zx::process FindProcess(zx::job& job, zx_koid_t pid);
zx::process FindProcess(zx_handle_t job, zx_koid_t pid);

}  // namespace debugger_utils

#endif  // GARNET_LIB_DEBUGGER_UTILS_JOBS_H_
