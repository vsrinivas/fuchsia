// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <zircon/syscalls.h>

#include <zxtest/zxtest.h>

TEST(CprngTestCase, DrawSuccess) {
  uint8_t buf[ZX_CPRNG_DRAW_MAX_LEN] = {0};
  zx_cprng_draw(buf, sizeof(buf));

  int num_zeros = 0;
  for (unsigned int i = 0; i < sizeof(buf); ++i) {
    if (buf[i] == 0) {
      num_zeros++;
    }
  }
  // The probability of getting more than 16 zeros if the buf is 256 bytes
  // is 6.76 * 10^-16, so probably not gonna happen.
  EXPECT_LE(num_zeros, 16, "buffer wasn't written to");
}

TEST(CprngTestCase, AddEntropyBadBuffer) {
  uint8_t buf[ZX_CPRNG_ADD_ENTROPY_MAX_LEN];
  zx_status_t status = zx_cprng_add_entropy((void*)4, sizeof(buf));
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS, "");
}

TEST(CprngTestCase, AddEntropyBufferTooLarge) {
  uint8_t buf[ZX_CPRNG_ADD_ENTROPY_MAX_LEN + 1];
  zx_status_t status = zx_cprng_add_entropy(buf, sizeof(buf));
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS, "");
}
