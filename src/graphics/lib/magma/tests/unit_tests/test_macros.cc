// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "magma_util/macros.h"

TEST(MagmaUtil, GetPow2) {
  uint64_t pow2;

  EXPECT_FALSE(magma::get_pow2(0, &pow2));
  EXPECT_FALSE(magma::get_pow2(3, &pow2));

  EXPECT_TRUE(magma::get_pow2(1, &pow2));
  EXPECT_EQ(pow2, 0ul);

  EXPECT_TRUE(magma::get_pow2(2, &pow2));
  EXPECT_EQ(pow2, 1ul);

  EXPECT_TRUE(magma::get_pow2(4, &pow2));
  EXPECT_EQ(pow2, 2ul);

  EXPECT_TRUE(magma::get_pow2(8, &pow2));
  EXPECT_EQ(pow2, 3ul);

  EXPECT_TRUE(magma::get_pow2(16, &pow2));
  EXPECT_EQ(pow2, 4ul);

  EXPECT_TRUE(magma::get_pow2(PAGE_SIZE, &pow2));
  EXPECT_EQ(pow2, 12ul);
}

TEST(MagmaUtil, RoundUp) {
  EXPECT_EQ(magma::round_up(0, 1), 0);
  EXPECT_EQ(magma::round_up(0, 2), 0);
  EXPECT_EQ(magma::round_up(0, 4), 0);
  EXPECT_EQ(magma::round_up(0, 4096), 0);

  EXPECT_EQ(magma::round_up(1, 1), 1);
  EXPECT_EQ(magma::round_up(1, 2), 2);
  EXPECT_EQ(magma::round_up(1, 4), 4);
  EXPECT_EQ(magma::round_up(1, 4096), 4096);

  EXPECT_EQ(magma::round_up(2, 1), 2);
  EXPECT_EQ(magma::round_up(2, 2), 2);
  EXPECT_EQ(magma::round_up(2, 4), 4);
  EXPECT_EQ(magma::round_up(2, 4096), 4096);

  EXPECT_EQ(magma::round_up(15, 16), 16);
  EXPECT_EQ(magma::round_up(16, 16), 16);
  EXPECT_EQ(magma::round_up(17, 16), 32);

  EXPECT_EQ(magma::round_up(PAGE_SIZE - 1, PAGE_SIZE), PAGE_SIZE);
  EXPECT_EQ(magma::round_up(PAGE_SIZE, PAGE_SIZE), PAGE_SIZE);
  EXPECT_EQ(magma::round_up(PAGE_SIZE + 1, PAGE_SIZE), PAGE_SIZE * 2);
}

TEST(MagmaUtil, Dret) {
  EXPECT_EQ(DRET(0), 0);
  EXPECT_EQ(DRET(-1), -1);

  EXPECT_EQ(DRET_MSG(0, "see this in a debug build only"), 0);
  EXPECT_EQ(DRET_MSG(-1, "see this in a debug build only: the number 1 [%d]", 1), -1);

  EXPECT_TRUE(DRETF(true, "never see this"));
  EXPECT_FALSE(DRETF(false, "see this in a debug build only"));
  EXPECT_FALSE(DRETF(false, "see this in a debug build only: the number 3 [%d]", 3));

  std::unique_ptr<int> myint(new int);
  EXPECT_EQ(DRETP(myint.get(), "never see this"), myint.get());

  EXPECT_EQ(DRETP(nullptr, "see this in a debug build only"), nullptr);
  EXPECT_EQ(DRETP(nullptr, "see this in a debug build only: the number four [%s]", "four"),
            nullptr);
}

TEST(MagmaUtil, ns_to_ms) {
  constexpr uint64_t kNsPerMs = 1000000;
  EXPECT_GE(UINT64_MAX / kNsPerMs, magma::ns_to_ms(UINT64_MAX));
  EXPECT_GE(INT64_MAX / kNsPerMs, magma::ns_to_ms(INT64_MAX));
  EXPECT_EQ(0u, magma::ns_to_ms(0));
  EXPECT_EQ(5u, magma::ns_to_ms(5 * kNsPerMs));
  EXPECT_EQ(5u, magma::ns_to_ms(6 * kNsPerMs - 1));
}

TEST(MagmaUtil, ms_to_signed_ns) {
  constexpr int64_t kNsPerMs = 1000000;
  EXPECT_EQ(INT64_MAX, magma::ms_to_signed_ns(UINT64_MAX));
  EXPECT_EQ(INT64_MAX, magma::ms_to_signed_ns(UINT64_MAX / kNsPerMs));
  const int64_t kMaxMs = INT64_MAX / kNsPerMs;
  EXPECT_EQ(kMaxMs * kNsPerMs, magma::ms_to_signed_ns(kMaxMs));
  EXPECT_EQ(INT64_MAX, magma::ms_to_signed_ns(kMaxMs + 1));
  EXPECT_EQ((kMaxMs - 1) * kNsPerMs, magma::ms_to_signed_ns(kMaxMs - 1));
  EXPECT_EQ(0u, magma::ms_to_signed_ns(0));
  EXPECT_EQ(kNsPerMs, magma::ms_to_signed_ns(1u));
}
