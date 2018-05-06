// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_DEBUGGER_UTILS_JOBS_H_
#define GARNET_LIB_DEBUGGER_UTILS_JOBS_H_

#include <functional>

#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>

namespace debugserver {
namespace util {

zx::job GetDefaultJob();

using JobTreeJobCallback = std::function<zx_status_t(
    zx::job& job, zx_koid_t koid, zx_koid_t parent_koid, int depth)>;

using JobTreeProcessCallback = std::function<zx_status_t(
    zx::process& process, zx_koid_t koid, zx_koid_t parent_koid, int depth)>;

using JobTreeThreadCallback = std::function<zx_status_t(
    zx::thread& thread, zx_koid_t koid, zx_koid_t parent_koid, int depth)>;

// Walk the job tree of |job|, beginning at |job| and descending to all its
// children.
// The walk continues until any callback returns something other than ZX_OK.
// By convention if tree walking has successfully completed and you want to
// "early exit" then return ZX_ERR_STOP from a callback.
//
// A reference to the task object is passed to each callback.
// If the callback wishes to take over ownership of the handle it can do so
// with task.release(). All other handles obtained during the walk are
// automagically closed by the time WalkJobTree() returns.
// Jobs are searched in depth-first order. If ownership of a job is taken by
// the callback then further decent into the tree is not possible. The walk
// will continue with any remaining tasks at the same level, but the walk
// will not look at the job's child jobs. Callers wishing a handle of a job,
// and decending into the job's child jobs, must make a duplicate handle.
// Making a duplicate handle is only necessary for jobs. Callbacks can freely
// call task.release() on processes and threads.
//
// TODO(dje): It's easy enough to allow the caller to take ownership of job
// handles and still continue decent into the tree, with the restriction that
// the caller can't close the handle until WalkJobTree() returns.

zx_status_t WalkJobTree(zx::job& job, JobTreeJobCallback* job_callback,
                        JobTreeProcessCallback* process_callback,
                        JobTreeThreadCallback* thread_callback);

// Simple wrapper on WalkJobTree for finding processes.
// Returns zx::process(ZX_HANDLE_INVALID) if not found.
zx::process FindProcess(zx::job& job, zx_koid_t pid);
zx::process FindProcess(zx_handle_t job, zx_koid_t pid);

}  // namespace util
}  // namespace debugserver

#endif  // GARNET_LIB_DEBUGGER_UTILS_JOBS_H_
