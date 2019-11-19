// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

#include <fbl/macros.h>
#include <ktl/popcount.h>

#include "tests.h"

namespace {

bool popcount32_test() {
  BEGIN_TEST;

  EXPECT_EQ(0, ktl::popcount<uint32_t>(0x00000000));
  EXPECT_EQ(8, ktl::popcount<uint32_t>(0x11111111));
  EXPECT_EQ(8, ktl::popcount<uint32_t>(0x22222222));
  EXPECT_EQ(16, ktl::popcount<uint32_t>(0x33333333));
  EXPECT_EQ(8, ktl::popcount<uint32_t>(0x44444444));
  EXPECT_EQ(16, ktl::popcount<uint32_t>(0x55555555));
  EXPECT_EQ(16, ktl::popcount<uint32_t>(0x66666666));
  EXPECT_EQ(24, ktl::popcount<uint32_t>(0x77777777));
  EXPECT_EQ(8, ktl::popcount<uint32_t>(0x88888888));
  EXPECT_EQ(16, ktl::popcount<uint32_t>(0x99999999));
  EXPECT_EQ(16, ktl::popcount<uint32_t>(0xAAAAAAAA));
  EXPECT_EQ(24, ktl::popcount<uint32_t>(0xBBBBBBBB));
  EXPECT_EQ(16, ktl::popcount<uint32_t>(0xCCCCCCCC));
  EXPECT_EQ(24, ktl::popcount<uint32_t>(0xDDDDDDDD));
  EXPECT_EQ(24, ktl::popcount<uint32_t>(0xEEEEEEEE));
  EXPECT_EQ(32, ktl::popcount<uint32_t>(0xFFFFFFFF));
  EXPECT_EQ(16, ktl::popcount<uint32_t>(0xAAAAAAAA));
  EXPECT_EQ(16, ktl::popcount<uint32_t>(0x55555555));
  EXPECT_EQ(16, ktl::popcount<uint32_t>(0x33333333));
  EXPECT_EQ(16, ktl::popcount<uint32_t>(0xCCCCCCCC));
  EXPECT_EQ(16, ktl::popcount<uint32_t>(0x66666666));
  EXPECT_EQ(8, ktl::popcount<uint32_t>(0x18181818));
  EXPECT_EQ(14, ktl::popcount<uint32_t>(0x1A980EF1));
  EXPECT_EQ(17, ktl::popcount<uint32_t>(0x365D906F));
  EXPECT_EQ(15, ktl::popcount<uint32_t>(0x73044E7C));
  EXPECT_EQ(14, ktl::popcount<uint32_t>(0x709E02CE));
  EXPECT_EQ(17, ktl::popcount<uint32_t>(0xCB22E1FC));
  EXPECT_EQ(13, ktl::popcount<uint32_t>(0x40694CC7));
  EXPECT_EQ(15, ktl::popcount<uint32_t>(0x2D0967CC));
  EXPECT_EQ(14, ktl::popcount<uint32_t>(0x544BA3C1));

  END_TEST;
}

bool popcount64_test() {
  BEGIN_TEST;

  EXPECT_EQ(0, ktl::popcount<uint64_t>(0x0000000000000000));
  EXPECT_EQ(16, ktl::popcount<uint64_t>(0x1111111111111111));
  EXPECT_EQ(16, ktl::popcount<uint64_t>(0x2222222222222222));
  EXPECT_EQ(32, ktl::popcount<uint64_t>(0x3333333333333333));
  EXPECT_EQ(16, ktl::popcount<uint64_t>(0x4444444444444444));
  EXPECT_EQ(32, ktl::popcount<uint64_t>(0x5555555555555555));
  EXPECT_EQ(32, ktl::popcount<uint64_t>(0x6666666666666666));
  EXPECT_EQ(48, ktl::popcount<uint64_t>(0x7777777777777777));
  EXPECT_EQ(16, ktl::popcount<uint64_t>(0x8888888888888888));
  EXPECT_EQ(32, ktl::popcount<uint64_t>(0x9999999999999999));
  EXPECT_EQ(32, ktl::popcount<uint64_t>(0xAAAAAAAAAAAAAAAA));
  EXPECT_EQ(48, ktl::popcount<uint64_t>(0xBBBBBBBBBBBBBBBB));
  EXPECT_EQ(32, ktl::popcount<uint64_t>(0xCCCCCCCCCCCCCCCC));
  EXPECT_EQ(48, ktl::popcount<uint64_t>(0xDDDDDDDDDDDDDDDD));
  EXPECT_EQ(48, ktl::popcount<uint64_t>(0xEEEEEEEEEEEEEEEE));
  EXPECT_EQ(64, ktl::popcount<uint64_t>(0xFFFFFFFFFFFFFFFF));
  EXPECT_EQ(32, ktl::popcount<uint64_t>(0xAAAAAAAAAAAAAAAA));
  EXPECT_EQ(32, ktl::popcount<uint64_t>(0x5555555555555555));
  EXPECT_EQ(32, ktl::popcount<uint64_t>(0x3333333333333333));
  EXPECT_EQ(32, ktl::popcount<uint64_t>(0xCCCCCCCCCCCCCCCC));
  EXPECT_EQ(32, ktl::popcount<uint64_t>(0x6666666666666666));
  EXPECT_EQ(16, ktl::popcount<uint64_t>(0x1818181818181818));
  EXPECT_EQ(22, ktl::popcount<uint64_t>(0x5D082F020202FC84));
  EXPECT_EQ(31, ktl::popcount<uint64_t>(0x1D1B5C1D0BD09676));
  EXPECT_EQ(36, ktl::popcount<uint64_t>(0x2E35DDF4B958A6F8));
  EXPECT_EQ(27, ktl::popcount<uint64_t>(0xAAEE326421692C22));
  EXPECT_EQ(29, ktl::popcount<uint64_t>(0xA6A270643F48E26C));
  EXPECT_EQ(35, ktl::popcount<uint64_t>(0xB9D42774D92B9E39));
  EXPECT_EQ(36, ktl::popcount<uint64_t>(0x7C1DE6347A7BD41B));
  EXPECT_EQ(34, ktl::popcount<uint64_t>(0x69C8F8C4EB58F2B9));

  END_TEST;
}

bool popcount_disallowed_types_test() {
  BEGIN_TEST;

#if TEST_WILL_NOT_COMPILE || 0
  ASSERT_EQ(-1, ktl::popcount(12.7));
#endif

#if TEST_WILL_NOT_COMPILE || 0
  ASSERT_EQ(-1, ktl::popcount(-7));
#endif

#if TEST_WILL_NOT_COMPILE || 0
  struct Foo {
    int x;
  } foo{5};
  ASSERT_EQ(-1, ktl::popcount(foo));
#endif

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(popcount_tests)
UNITTEST("32 bit", popcount32_test)
UNITTEST("64 bit", popcount64_test)
UNITTEST("disallowed_types", popcount_disallowed_types_test)
UNITTEST_END_TESTCASE(popcount_tests, "popcount", "Unit tests for popcount")
