// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/backoff/exponential_backoff.h"

#include <stdlib.h>

#include "lib/ftl/logging.h"
#include "lib/ftl/time/time_delta.h"

namespace backoff {

ExponentialBackoff::ExponentialBackoff(std::function<uint64_t()> seed_generator)
    : ExponentialBackoff(ftl::TimeDelta::FromMilliseconds(100),
                         2u,
                         ftl::TimeDelta::FromSeconds(60 * 60),
                         seed_generator) {}

ExponentialBackoff::ExponentialBackoff(
    ftl::TimeDelta initial_delay,
    uint32_t retry_factor,
    ftl::TimeDelta max_delay,
    std::function<uint64_t()> seed_generator)
    : initial_delay_(initial_delay),
      retry_factor_(retry_factor),
      max_delay_(max_delay),
      max_delay_divided_by_factor_(max_delay_ / retry_factor_),
      rng_(seed_generator()) {
  FTL_DCHECK(ftl::TimeDelta() < initial_delay_ && initial_delay_ <= max_delay_);
  FTL_DCHECK(0 < retry_factor_);
  FTL_DCHECK(ftl::TimeDelta() <= max_delay_);
}

ExponentialBackoff::~ExponentialBackoff() {}

ftl::TimeDelta ExponentialBackoff::GetNext() {
  // Add a random component in [0, next_delay).
  std::uniform_int_distribution<uint64_t> distribution(
      0u, next_delay_.ToMilliseconds());
  ftl::TimeDelta r = ftl::TimeDelta::FromMilliseconds(distribution(rng_));
  ftl::TimeDelta result =
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
