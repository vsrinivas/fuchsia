// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/barrier.h>

namespace fit {

barrier::barrier() {
  // Capture a new consumer and intentionally abandon its associated
  // completer so that a promise chained onto the consumer using
  // |promise_or()| will become immediately runnable.
  fit::bridge<> bridge;
  prior_ = std::move(bridge.consumer);
}

barrier::~barrier() = default;

fit::consumer<> barrier::swap_prior(fit::consumer<> new_prior) {
  std::lock_guard<std::mutex> lock(mutex_);
  fit::consumer<> old_prior = std::move(prior_);
  prior_ = std::move(new_prior);
  return old_prior;
}

}  // namespace fit
