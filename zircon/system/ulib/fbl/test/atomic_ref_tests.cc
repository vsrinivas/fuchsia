// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/atomic_ref.h>
#include <zxtest/zxtest.h>

namespace {

// Functional test of each member function of fbl::atomic_ref
TEST(AtomicRef, BasicTest) {
  int val = 0;

  fbl::atomic_ref<int> atomic_ref_1(val);
  EXPECT_EQ(true, atomic_ref_1.is_always_lock_free);

  atomic_ref_1 = 1;
  EXPECT_EQ(true, atomic_ref_1.is_lock_free());
  EXPECT_EQ(1, val);  // Check that the underlying storage is updated
  atomic_ref_1.store(2);
  EXPECT_EQ(2, atomic_ref_1.load());

  EXPECT_EQ(2, atomic_ref_1.exchange(0));
  EXPECT_EQ(0, atomic_ref_1.exchange(2, fbl::memory_order_acq_rel));

  atomic_ref_1 = 0;
  int expected = 0;
  EXPECT_EQ(true, atomic_ref_1.compare_exchange_strong(expected, 1));
  EXPECT_EQ(1, atomic_ref_1.load());
  EXPECT_EQ(0, expected);
  EXPECT_EQ(false, atomic_ref_1.compare_exchange_strong(expected, 1));
  EXPECT_EQ(1, expected);

  atomic_ref_1 = 0;
  EXPECT_EQ(0, atomic_ref_1.fetch_add(1));
  EXPECT_EQ(1, atomic_ref_1.load());

  atomic_ref_1 = 1;
  EXPECT_EQ(1, atomic_ref_1.fetch_sub(1));
  EXPECT_EQ(0, atomic_ref_1.load());

  atomic_ref_1 = 2;
  EXPECT_EQ(2, atomic_ref_1.fetch_and(1));
  EXPECT_EQ(0, atomic_ref_1.load());

  atomic_ref_1 = 2;
  EXPECT_EQ(2, atomic_ref_1.fetch_or(1));
  EXPECT_EQ(3, atomic_ref_1.load());

  atomic_ref_1 = 2;
  EXPECT_EQ(2, atomic_ref_1.fetch_xor(2));
  EXPECT_EQ(0, atomic_ref_1.load());

  atomic_ref_1 = 0;
  EXPECT_EQ(0, atomic_ref_1++);
  EXPECT_EQ(1, atomic_ref_1.load());

  atomic_ref_1 = 0;
  EXPECT_EQ(1, ++atomic_ref_1);
  EXPECT_EQ(1, atomic_ref_1.load());

  atomic_ref_1 = 1;
  EXPECT_EQ(1, atomic_ref_1--);
  EXPECT_EQ(0, atomic_ref_1.load());

  atomic_ref_1 = 1;
  EXPECT_EQ(0, --atomic_ref_1);
  EXPECT_EQ(0, atomic_ref_1.load());

  atomic_ref_1 = 0;
  atomic_ref_1 += 2;
  EXPECT_EQ(2, atomic_ref_1.load());

  atomic_ref_1 = 2;
  atomic_ref_1 -= 2;
  EXPECT_EQ(0, atomic_ref_1.load());

  atomic_ref_1 = 1;
  atomic_ref_1 &= -1;
  EXPECT_EQ(1, atomic_ref_1.load());

  atomic_ref_1 = 1;
  atomic_ref_1 |= 2;
  EXPECT_EQ(3, atomic_ref_1.load());

  atomic_ref_1 = 1;
  atomic_ref_1 ^= 2;
  EXPECT_EQ(3, atomic_ref_1.load());

  atomic_ref_1.store(1);
  fbl::atomic_ref<int> atomic_ref_2(atomic_ref_1);
  EXPECT_EQ(1, atomic_ref_2.load());
}

// Basic test of qualified integral types
TEST(AtomicRef, BasicQualified) {
  volatile uint32_t i = 0;
  fbl::atomic_ref<volatile uint32_t> i_ref(i);
  i_ref.store(1);
  EXPECT_EQ(1, i_ref.load());
  EXPECT_EQ(1, i_ref.exchange(2));
}

}  // anonymous namespace
