// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/random.h>
#include <lib/unittest/unittest.h>

#include <ktl/type_traits.h>

#include <ktl/enforce.h>

namespace {

template <bool Reseeded>
bool ArchRandomTest() {
  BEGIN_TEST;

  bool supported = arch::Random<Reseeded>::Supported();
  EXPECT_EQ(supported, !!supported);

  if (supported) {
    auto result = arch::Random<Reseeded>::Get();
    ASSERT_TRUE(result);
    auto value = result.value();
    static_assert(ktl::is_same_v<decltype(value), uint64_t>);
  }

  END_TEST;
}

bool PlainRandomTest() { return ArchRandomTest<false>(); }

bool ReseededRandomTest() { return ArchRandomTest<true>(); }

}  // namespace

UNITTEST_START_TESTCASE(ArchRandomTests)
UNITTEST("hardware RNG", PlainRandomTest)
UNITTEST("hardware reseeded RNG", ReseededRandomTest)
UNITTEST_END_TESTCASE(ArchRandomTests, "arch-random", "hardware RNG tests")
