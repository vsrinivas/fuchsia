// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>
#include <wlan/drivers/fuzzing.h>
#include <wlan/drivers/log.h>

namespace fuzzing = wlan::drivers::fuzzing;

const uint32_t RAND_TEST_REPETITIONS = 1000;

#define RAND_TEST(R, rand_type)                                                              \
  TEST(FuzzingTest, UnlikelyRepeats_##rand_type) {                                           \
    std::random_device::result_type seed;                                                    \
    auto rng = fuzzing::seeded_rng(&seed);                                                   \
    const char* test_name = testing::UnitTest::GetInstance()->current_test_info()->name();   \
    linfo("%s seed: %u", test_name, seed);                                                   \
    rand_type r = R(rng);                                                                    \
    size_t i = 0;                                                                            \
    for (; i < RAND_TEST_REPETITIONS; ++i) {                                                 \
      rand_type r_next = R(rng);                                                             \
      if (r != r_next) {                                                                     \
        break;                                                                               \
      }                                                                                      \
      r = r_next;                                                                            \
    }                                                                                        \
    if (i == RAND_TEST_REPETITIONS) {                                                        \
      FAIL() << #R << "() produces same value " << RAND_TEST_REPETITIONS << " times: " << r; \
    }                                                                                        \
  }

RAND_TEST(fuzzing::rand8, uint8_t)
RAND_TEST(fuzzing::rand16, uint16_t)
RAND_TEST(fuzzing::rand32, uint32_t)
RAND_TEST(fuzzing::rand64, uint64_t)
