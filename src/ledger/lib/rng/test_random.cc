// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/rng/test_random.h"

#include <algorithm>

namespace ledger {

TestRandom::TestRandom(std::uint_fast64_t seed) : char_engine_(std::mt19937_64(seed)) {}

void TestRandom::InternalDraw(void* buffer, size_t buffer_size) {
  uint8_t* char_buffer = static_cast<uint8_t*>(buffer);
  std::generate(char_buffer, char_buffer + buffer_size, std::ref(char_engine_));
}

}  // namespace ledger
