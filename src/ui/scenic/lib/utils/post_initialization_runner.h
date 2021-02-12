// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_UTILS_POST_INITIALIZATION_RUNNER_H_
#define SRC_UI_SCENIC_LIB_UTILS_POST_INITIALIZATION_RUNNER_H_

#include <lib/fit/function.h>

#include <vector>

namespace utils {

// Helper which either runs closures immediately (if already initialized), or which enqueues them
// for later execution (if not already initialized).  Not thread safe.
class PostInitializationRunner final {
 public:
  PostInitializationRunner() = default;
  ~PostInitializationRunner() = default;

  // Idempotent.  The first time this is called, all enqueued run.  Subsequently, closures are run
  // immediately instead of being enqueued.
  void SetInitialized();

  void RunAfterInitialized(fit::closure closure);

 private:
  bool initialized_ = false;
  // Closures that will be run when all systems are initialized.
  std::vector<fit::closure> run_after_initialized_;
};

}  // namespace utils

#endif  // SRC_UI_SCENIC_LIB_UTILS_POST_INITIALIZATION_RUNNER_H_
