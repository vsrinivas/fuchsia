// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fpromise/barrier.h>

namespace fpromise {

barrier::barrier() {
  // Capture a new consumer and intentionally abandon its associated
  // completer so that a promise chained onto the consumer using
  // |promise_or()| will become immediately runnable.
  fpromise::bridge<> bridge;
  prior_ = std::move(bridge.consumer);
}

barrier::~barrier() = default;

fpromise::consumer<> barrier::swap_prior(fpromise::consumer<> new_prior) {
  std::lock_guard<std::mutex> lock(mutex_);
  fpromise::consumer<> old_prior = std::move(prior_);
  prior_ = std::move(new_prior);
  return old_prior;
}

}  // namespace fpromise
