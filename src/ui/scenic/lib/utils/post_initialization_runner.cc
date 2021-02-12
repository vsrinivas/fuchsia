// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/utils/post_initialization_runner.h"

namespace utils {

void PostInitializationRunner::SetInitialized() {
  if (initialized_)
    return;
  initialized_ = true;
  for (auto& closure : run_after_initialized_) {
    closure();
  }
  run_after_initialized_.clear();
}

void PostInitializationRunner::RunAfterInitialized(fit::closure closure) {
  if (initialized_) {
    closure();
  } else {
    run_after_initialized_.push_back(std::move(closure));
  }
}

}  // namespace utils
