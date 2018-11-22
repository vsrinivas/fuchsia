// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Can't compile this for Zircon userspace yet since libstdc++ isn't available.
#ifndef FIT_NO_STD_FOR_ZIRCON_USERSPACE

#include <lib/fit/sequencer.h>

namespace fit {

sequencer::sequencer() {
    // Capture a new consumer and intentionally abandon its associated
    // completer so that a promise chained onto the consumer using
    // |promise_or()| will become immediately runnable.
    fit::bridge<> bridge;
    prior_ = std::move(bridge.consumer());
}

sequencer::~sequencer() = default;

fit::consumer<> sequencer::swap_prior(fit::consumer<> new_prior) {
    std::lock_guard<std::mutex> lock(mutex_);
    fit::consumer<> old_prior = std::move(prior_);
    prior_ = std::move(new_prior);
    return old_prior;
}

} // namespace fit

#endif // FIT_NO_STD_FOR_ZIRCON_USERSPACE
