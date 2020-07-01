// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/time.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/syslog/cpp/macros.h>
#include <stdint.h>

#include <gtest/gtest.h>
#include <openthread/platform/entropy.h>

TEST(Entropy, BasicEntropyCheck) {
  constexpr auto kNumPossibleValues = UINT8_MAX + 1;
  constexpr auto kNumExpectedRepetitions = 100;
  constexpr auto kNumRandValues = kNumPossibleValues * kNumExpectedRepetitions;
  uint8_t values[kNumRandValues];
  EXPECT_EQ(otPlatEntropyGet(values, kNumRandValues), OT_ERROR_NONE);

  size_t i;
  int32_t hist[kNumPossibleValues] = {0};
  for (i = 0; i < kNumRandValues; i++) {
    hist[values[i]]++;
  }

  // The expected value of hist[i] is kNumExpectedRepetitions (100 in this case).
  // For each hist[i]
  // the probability of any value below 10% of expected is extremely low
  // for a decent entropy source and for kNumExpectedRepetitions = 100
  // The probability is computed as:
  //   25600-C-0 * (1/256)^0 * (255/256)^(25600)
  // + 25600-C-1 * (1/256)^1 * (255/256)^(25600-1)
  // + 25600-C-2 * (1/256)^2 * (255/256)^(25600-2)
  // ...
  // + 25600-C-9 * (1/256)^9 * (255/256)^(25600-9)
  // and comes out to be (10)^-31
  for (i = 0; i < kNumPossibleValues; i++) {
    EXPECT_GE(hist[i], kNumExpectedRepetitions / 10);
  }
}

TEST(Enropy, InvalidArgs) {
  constexpr auto kValidSize = 5;
  constexpr auto kInvalidSize = 0;
  uint8_t array[kValidSize];

  // Invalid array:
  EXPECT_EQ(otPlatEntropyGet(nullptr, kValidSize), OT_ERROR_INVALID_ARGS);

  // Invalid size:
  EXPECT_EQ(otPlatEntropyGet(array, kInvalidSize), OT_ERROR_INVALID_ARGS);

  // Both invalid
  EXPECT_EQ(otPlatEntropyGet(NULL, kInvalidSize), OT_ERROR_INVALID_ARGS);
}
