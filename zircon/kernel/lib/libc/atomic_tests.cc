// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

#include <ktl/atomic.h>

namespace {

constexpr unsigned __int128 kValue =
    (static_cast<unsigned __int128>(0x0123456789abcdef) << 64) + 0xfedcba9876543210;

bool Load16Test() {
  BEGIN_TEST;

  {
    ktl::atomic<unsigned __int128> v = 0u;
    EXPECT_EQ(v.load(), 0u);
  }

  {
    ktl::atomic<unsigned __int128> v = kValue;
    EXPECT_EQ(v.load(), kValue);
  }

  END_TEST;
}

bool Store16Test() {
  BEGIN_TEST;

  ktl::atomic<unsigned __int128> v = 0u;
  EXPECT_EQ(v.load(), 0u);
  v.store(kValue);
  EXPECT_EQ(v.load(), kValue);

  END_TEST;
}

bool CompareExchange16Test() {
  BEGIN_TEST;

  {
    // Comparison fails.
    ktl::atomic<unsigned __int128> v = kValue;
    unsigned __int128 expected = kValue - 1;
    EXPECT_FALSE(v.compare_exchange_strong(expected, 0u));
    EXPECT_EQ(expected, kValue);
    EXPECT_EQ(v.load(), kValue);
  }

  {
    // Comparison succeeds.
    ktl::atomic<unsigned __int128> v = kValue;
    unsigned __int128 expected = kValue;
    constexpr unsigned __int128 kDesired = 0xaaaabbbbccccdddd;
    EXPECT_TRUE(v.compare_exchange_strong(expected, kDesired));
    EXPECT_EQ(expected, kValue);
    EXPECT_EQ(v.load(), kDesired);
  }

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(libc_atomic_tests)
UNITTEST("load_16", Load16Test)
UNITTEST("store_16", Store16Test)
UNITTEST("compare_exchange_16", CompareExchange16Test)
UNITTEST_END_TESTCASE(libc_atomic_tests, "libc_atomic", "libc/atomic tests")
