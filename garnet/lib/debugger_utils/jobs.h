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

using JobTreeJobCallback =
    std::function<zx_status_t(zx::job* job, zx_koid_t koid, zx_koid_t parent_koid, int depth)>;

using JobTreeProcessCallback = std::function<zx_status_t(zx::process* process, zx_koid_t koid,
                                                         zx_koid_t parent_koid, int depth)>;

using JobTreeThreadCallback = std::function<zx_status_t(zx::thread* thread, zx_koid_t koid,
                                                        zx_koid_t parent_koid, int depth)>;

// Walk the job tree of |job|. Jobs are searched in depth-first order, except
// that |job| itself is not passed to the job callback. If the caller wants to
// analyze job it can do so directly. It is done this way so that the job can
// be passed as a const reference (which is the only thing that makes sense).
//
// A pointer to the zx::task object (job,process,thread) is passed to each
// callback. If the callback wishes to take over ownership of the handle it
// can do so with task->release(), with one caveat for job and process handles:
// Even though the handle is now owned by the callback, it is loaned to
// WalkJobTree, and the callback must not close/replace/transfer/whatever the
// handle until WalkJobTree returns.
// All other handles obtained during the walk are automagically closed by the
// time WalkJobTree() returns.
//
// The walk continues until any callback returns something other than ZX_OK.
// By convention if tree walking has successfully completed and you want to
// "early exit" then return ZX_ERR_STOP from a callback.

zx_status_t WalkJobTree(const zx::job& job, JobTreeJobCallback* job_callback,
                        JobTreeProcessCallback* process_callback,
                        JobTreeThreadCallback* thread_callback);

// Simple wrapper on WalkJobTree for finding processes.
// Returns zx::process(ZX_HANDLE_INVALID) if not found.
zx::process FindProcess(const zx::job& job, zx_koid_t pid);
zx::process FindProcess(zx_handle_t job, zx_koid_t pid);

}  // namespace debugger_utils

#endif  // GARNET_LIB_DEBUGGER_UTILS_JOBS_H_
