// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_BACKOFF_EXPONENTIAL_BACKOFF_H_
#define APPS_LEDGER_SRC_BACKOFF_EXPONENTIAL_BACKOFF_H_

#include <random>

#include "apps/ledger/src/backoff/backoff.h"
#include "apps/ledger/src/glue/crypto/rand.h"

namespace backoff {

// Exponential backoff. The returned backoff delay is D + r:
//   D = |initial_delay| * |retry_factor| ^ N
//   r = rand(0, D)
// with N denoting the number of consecutive GetNext() calls, starting at 0.
class ExponentialBackoff : public Backoff {
 public:
  explicit ExponentialBackoff(
      std::function<uint64_t()> seed_generator = glue::RandUint64);
  ExponentialBackoff(
      fxl::TimeDelta initial_delay,
      uint32_t retry_factor,
      fxl::TimeDelta max_delay,
      std::function<uint64_t()> seed_generator = glue::RandUint64);
  ~ExponentialBackoff() override;

  fxl::TimeDelta GetNext() override;
  void Reset() override;

 private:
  const fxl::TimeDelta initial_delay_;
  const uint32_t retry_factor_;
  const fxl::TimeDelta max_delay_;
  // Used to prevent overflows in multiplication.
  const fxl::TimeDelta max_delay_divided_by_factor_;
  std::default_random_engine rng_;

  fxl::TimeDelta next_delay_ = initial_delay_;
};

}  // namespace backoff

#endif  // APPS_LEDGER_SRC_BACKOFF_EXPONENTIAL_BACKOFF_H_
