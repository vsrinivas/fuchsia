// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <ktl/array.h>
#include <lib/relaxed_atomic.h>
#include <lib/unittest/unittest.h>

namespace {

bool PrimaryTypeTest() {
  BEGIN_TEST;

  RelaxedAtomic<bool> value{true};
  ASSERT_TRUE(value);
  ASSERT_TRUE(value.load());

  value.store(false);
  ASSERT_FALSE(value);
  ASSERT_FALSE(value.load());

  END_TEST;
}

bool DerivedTypeTest() {
  BEGIN_TEST;

  constexpr auto kSize = 4;
  RelaxedAtomic<ktl::array<bool, kSize>> value{{}};
  for (int i = 0; i < kSize; i++) {
    ASSERT_FALSE(value.load()[i]);
  }

  value.store(ktl::array<bool, kSize>{true, true, true, true});
  for (int i = 0; i < kSize; i++) {
    ASSERT_TRUE(value.load()[i]);
  }
  END_TEST;
}

bool UserTypeTest() {
  BEGIN_TEST;

  struct Payload {
    uint32_t value_a = 0;
    uint16_t value_b = 0;
    uint8_t value_c = 0;
    uint8_t value_d = 0;
  };

  RelaxedAtomic<Payload> value{{}};
  ASSERT_EQ(0u, value.load().value_a);
  ASSERT_EQ(0u, value.load().value_b);
  ASSERT_EQ(0u, value.load().value_c);
  ASSERT_EQ(0u, value.load().value_d);

  value.store(Payload{.value_a = 1024, .value_b = 512, .value_c = 255, .value_d = 128});
  ASSERT_EQ(1024u, value.load().value_a);
  ASSERT_EQ(512u, value.load().value_b);
  ASSERT_EQ(255u, value.load().value_c);
  ASSERT_EQ(128u, value.load().value_d);

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(relaxed_atomic_tests)
  UNITTEST("Primary type test", PrimaryTypeTest)
  UNITTEST("Derived type test", DerivedTypeTest)
  UNITTEST("User type test", UserTypeTest)
UNITTEST_END_TESTCASE(relaxed_atomic_tests, "relaxed_atomic",
                      "Tests for the relaxed atomic wrapper.")
