// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/backoff/exponential_backoff.h"

#include <stdlib.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_delta.h"

namespace backoff {

ExponentialBackoff::ExponentialBackoff(std::function<uint64_t()> seed_generator)
    : ExponentialBackoff(fxl::TimeDelta::FromMilliseconds(100),
                         2u,
                         fxl::TimeDelta::FromSeconds(60 * 60),
                         seed_generator) {}

ExponentialBackoff::ExponentialBackoff(fxl::TimeDelta initial_delay,
                                       uint32_t retry_factor,
                                       fxl::TimeDelta max_delay,
                                       std::function<uint64_t()> seed_generator)
    : initial_delay_(initial_delay),
      retry_factor_(retry_factor),
      max_delay_(max_delay),
      max_delay_divided_by_factor_(max_delay_ / retry_factor_),
      rng_(seed_generator()) {
  FXL_DCHECK(fxl::TimeDelta() <= initial_delay_ &&
             initial_delay_ <= max_delay_);
  FXL_DCHECK(0 < retry_factor_);
  FXL_DCHECK(fxl::TimeDelta() <= max_delay_);
}

ExponentialBackoff::~ExponentialBackoff() {}

fxl::TimeDelta ExponentialBackoff::GetNext() {
  // Add a random component in [0, next_delay).
  std::uniform_int_distribution<uint64_t> distribution(
      0u, next_delay_.ToMilliseconds());
  fxl::TimeDelta r = fxl::TimeDelta::FromMilliseconds(distribution(rng_));
  fxl::TimeDelta result =
      max_delay_ - r >= next_delay_ ? next_delay_ + r : max_delay_;

  // Calculate the next delay.
  next_delay_ = next_delay_ <= max_delay_divided_by_factor_
                    ? next_delay_ * retry_factor_
                    : max_delay_;
  return result;
}

void ExponentialBackoff::Reset() {
  next_delay_ = initial_delay_;
}

}  // namespace backoff
