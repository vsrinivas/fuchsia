// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <lib/zbitl/view.h>

#include <ktl/span.h>

#include "tests.h"

// zbitl is primarily tested by its host/userland unit tests.
// This serves to test some basic cases in the kernel and phys
// environments specifically, mostly just to make sure it compiles.

namespace {

// `zbi --output=empty.zbi; hexdump -v -e '1/1 "\\x%02x"' empty.zbi`.
alignas(ZBI_ALIGNMENT) constexpr char kEmptyZbi[] =
    "\x42\x4f\x4f\x54\x00\x00\x00\x00\xe6\xf7\x8c\x86\x00\x00\x01\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x29\x17\x78\xb5\xd6\xe8\x87\x4a";

bool EmptyZbiTest() {
  BEGIN_TEST;

  zbitl::View zbi(ktl::span<const char>{kEmptyZbi, sizeof(kEmptyZbi)});

  for (auto [header, payload] : zbi) {
    EXPECT_EQ(header->type, header->type);
    EXPECT_TRUE(false, "should not be reached");
  }

  auto error = zbi.take_error();
  ASSERT_TRUE(error.is_ok(), error.error_value().zbi_error.data());

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(zbitl_tests)
UNITTEST("empty", EmptyZbiTest)
UNITTEST_END_TESTCASE(zbitl_tests, "zbitl", "Tests of ZBI template library")
