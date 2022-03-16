// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <random>

#include <wlan/drivers/fuzzing.h>

namespace wlan::drivers::fuzzing {

static std::uniform_int_distribution<uint16_t> rand8_(0, UINT8_MAX);
uint8_t rand8(std::default_random_engine& rng) { return static_cast<uint8_t>(rand8_(rng)); }

std::default_random_engine seeded_rng(std::random_device::result_type* out_seed) {
  std::random_device rd;
  std::random_device::result_type seed = rd();
  if (out_seed != nullptr) {
    *out_seed = seed;
  }
  return std::default_random_engine(seed);
}

}  // namespace wlan::drivers::fuzzing
