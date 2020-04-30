// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <zxtest/zxtest.h>

#if ENABLE_ENTROPY_COLLECTOR_TEST

TEST(Entropy, FileExists) {
  FILE* file = fopen("/boot/kernel/debug/entropy.bin", "rb");
  ASSERT_NONNULL(file, "entropy file doesn't exist");

  char buf[32];
  size_t read = fread(buf, 1, sizeof(buf), file);

  EXPECT_LT(0, read, "entropy file contains no data or not readable");
}

#endif
