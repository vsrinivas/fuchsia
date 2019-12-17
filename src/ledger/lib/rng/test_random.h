// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_RNG_TEST_RANDOM_H_
#define SRC_LEDGER_LIB_RNG_TEST_RANDOM_H_

#include <random>

#include "src/ledger/lib/rng/random.h"

namespace ledger {

// Implementation of |Random| that uses a PRNG.
class TestRandom final : public Random {
 public:
  TestRandom(std::uint_fast64_t seed);

 private:
  void InternalDraw(void* buffer, size_t buffer_size) override;

  std::independent_bits_engine<std::mt19937_64, 8, uint8_t> char_engine_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_LIB_RNG_TEST_RANDOM_H_
