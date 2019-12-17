// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "task_tree.h"

#include "src/lib/fxl/logging.h"

namespace harvester {

// Collect a new set of tasks (jobs/processes/threads). Note that this will
// clear out any prior task information.
void TaskTree::Gather() {
  Clear();
  WalkRootJobTree();
}

// Clear all jobs/processes/threads information. Note that this is called by
// Gather() and the destructor (i.e. no need for a separate call to Clear()
// for those cases).
void TaskTree::Clear() {
  // It may be worth checking if this can be  optimized by sending the handles
  // in batches.

  for (auto& job : jobs_) {
    zx_handle_close(job.handle);
  }
  jobs_.clear();

  for (auto& process : processes_) {
    zx_handle_close(process.handle);
  }
  processes_.clear();

  for (auto& thread : threads_) {
    zx_handle_close(thread.handle);
  }
  threads_.clear();
}

}  // namespace harvester.
