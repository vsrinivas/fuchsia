// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/random.h>
#include <sys/types.h>
#include <unistd.h>

#include <zxtest/zxtest.h>

TEST(GetrandomTest, Smoke) {
  uint8_t buffer[ZX_CPRNG_DRAW_MAX_LEN * 2];
  // Get only a single chunk of random data (within ZX_CPRNG_DRAW_MAX_LEN).
  ASSERT_EQ(ZX_CPRNG_DRAW_MAX_LEN, getrandom(buffer, ZX_CPRNG_DRAW_MAX_LEN, 0));
  // Get more than a single chunk of random data.
  ASSERT_EQ(sizeof(buffer), getrandom(buffer, sizeof(buffer), 0));
}

TEST(GetrandomTest, ValidateRandom) {
  uint8_t buffer[ZX_CPRNG_DRAW_MAX_LEN] = {0};
  getrandom(buffer, ZX_CPRNG_DRAW_MAX_LEN, 0);

  // Iterate over the buffer and confirm that there is at least one non-zero
  // byte. Although an all-zero buffer is technically valid random output, the
  // goal of this test is to ensure that the input buffer is not left unchanged.
  // The likelihood of this test flaking is low; the average time between flakes
  // is larger than the current age of the universe.
  auto non_zero_byte = std::find_if(std::begin(buffer), std::end(buffer),
                                    [](const uint8_t& byte) { return byte != 0; });
  ASSERT_NE(non_zero_byte, std::end(buffer));
}

TEST(GetrandomTest, ValidateFlags) {
  uint8_t buffer[2];
  ASSERT_EQ(sizeof(buffer), getrandom(buffer, sizeof(buffer), GRND_NONBLOCK));
  ASSERT_EQ(sizeof(buffer), getrandom(buffer, sizeof(buffer), GRND_RANDOM));
  ASSERT_EQ(sizeof(buffer), getrandom(buffer, sizeof(buffer), GRND_NONBLOCK | GRND_RANDOM));
  ASSERT_EQ(-1, getrandom(buffer, sizeof(buffer), ~(GRND_NONBLOCK | GRND_RANDOM)));
}
