// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_BACKOFF_EXPONENTIAL_BACKOFF_H_
#define LIB_BACKOFF_EXPONENTIAL_BACKOFF_H_

#include <random>

#include "lib/backoff/backoff.h"
#include "lib/fxl/random/rand.h"

namespace backoff {

// Exponential backoff. The returned backoff delay is D + r:
//   D = |initial_delay| * |retry_factor| ^ N
//   r = rand(0, D)
// with N denoting the number of consecutive GetNext() calls, starting at 0.
class ExponentialBackoff : public Backoff {
 public:
  explicit ExponentialBackoff(
      std::function<uint64_t()> seed_generator = fxl::RandUint64);
  ExponentialBackoff(
      zx::duration initial_delay, uint32_t retry_factor, zx::duration max_delay,
      std::function<uint64_t()> seed_generator = fxl::RandUint64);
  ~ExponentialBackoff() override;

  zx::duration GetNext() override;
  void Reset() override;

 private:
  const zx::duration initial_delay_;
  const uint32_t retry_factor_;
  const zx::duration max_delay_;
  // Used to prevent overflows in multiplication.
  const zx::duration max_delay_divided_by_factor_;
  std::default_random_engine rng_;

  zx::duration next_delay_ = initial_delay_;
};

}  // namespace backoff

#endif  // LIB_BACKOFF_EXPONENTIAL_BACKOFF_H_
