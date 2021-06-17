// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fpromise/sequencer.h>

namespace fpromise {

sequencer::sequencer() {
  // Capture a new consumer and intentionally abandon its associated
  // completer so that a promise chained onto the consumer using
  // |promise_or()| will become immediately runnable.
  fpromise::bridge<> bridge;
  prior_ = std::move(bridge.consumer);
}

sequencer::~sequencer() = default;

fpromise::consumer<> sequencer::swap_prior(fpromise::consumer<> new_prior) {
  std::lock_guard<std::mutex> lock(mutex_);
  fpromise::consumer<> old_prior = std::move(prior_);
  prior_ = std::move(new_prior);
  return old_prior;
}

}  // namespace fpromise
