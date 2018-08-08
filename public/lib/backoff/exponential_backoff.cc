// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/backoff/exponential_backoff.h"

#include <stdlib.h>

#include "lib/fxl/logging.h"

namespace backoff {

ExponentialBackoff::ExponentialBackoff(fit::function<uint64_t()> seed_generator)
    : ExponentialBackoff(zx::msec(100), 2u, zx::sec(60 * 60),
                         std::move(seed_generator)) {}

ExponentialBackoff::ExponentialBackoff(zx::duration initial_delay,
                                       uint32_t retry_factor,
                                       zx::duration max_delay,
                                       fit::function<uint64_t()> seed_generator)
    : initial_delay_(initial_delay),
      retry_factor_(retry_factor),
      max_delay_(max_delay),
      max_delay_divided_by_factor_(max_delay_ / retry_factor_),
      rng_(seed_generator()) {
  FXL_DCHECK(zx::duration() <= initial_delay_ && initial_delay_ <= max_delay_);
  FXL_DCHECK(0 < retry_factor_);
  FXL_DCHECK(zx::duration() <= max_delay_);
}

ExponentialBackoff::~ExponentialBackoff() {}

zx::duration ExponentialBackoff::GetNext() {
  // Add a random component in [0, next_delay).
  std::uniform_int_distribution<zx_duration_t> distribution(0u,
                                                            next_delay_.get());
  zx::duration r(distribution(rng_));
  zx::duration result =
      max_delay_ - r >= next_delay_ ? next_delay_ + r : max_delay_;

  // Calculate the next delay.
  next_delay_ = next_delay_ <= max_delay_divided_by_factor_
                    ? next_delay_ * retry_factor_
                    : max_delay_;
  return result;
}

void ExponentialBackoff::Reset() { next_delay_ = initial_delay_; }

}  // namespace backoff
