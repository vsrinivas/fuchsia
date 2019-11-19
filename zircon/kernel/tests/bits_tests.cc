// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <bits.h>
#include <stdint.h>

static bool bits_test(void) {
  BEGIN_TEST;
  EXPECT_EQ(0b1001u, (ExtractBits<3, 0, uint8_t>(0b10101001u)));
  EXPECT_EQ(0b1001u, (ExtractBits<5, 2, uint8_t>(0b1010100100u)));
  EXPECT_EQ(0b1001u, (ExtractBits<63, 60, uint64_t>(0x9000000000000000u)));
  EXPECT_EQ(0xe7c07357u, (ExtractBits<63, 32, uint32_t>(0xe7c0735700000000)));
  END_TEST;
}

UNITTEST_START_TESTCASE(bits_tests)
UNITTEST("bits lib tests", bits_test)
UNITTEST_END_TESTCASE(bits_tests, "bits", "bits lib tests")
