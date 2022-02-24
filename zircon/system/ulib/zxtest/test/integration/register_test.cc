// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <zxtest/base/test-info.h>
#include <zxtest/zxtest.h>

#include "helper.h"

namespace {
TEST(Test, AutoRegister) {}

class TestFixture : public zxtest::Test {
 public:
  ~TestFixture() override {}

  static void SetUpTestSuite() {}
  static void TearDownTestSuite() {}

 protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(TestFixture, AutoRegister) {}

void Verify() {
  auto* runner = zxtest::Runner::GetInstance();

  // This is using internals to obtain a handle on the test info, through the TestRef.
  // This will either fail at compile time because this variable does not exist(macro error),
  // or at runtime because the test failed to register (logic error).
  const zxtest::TestInfo& test_info = runner->GetTestInfo(LIB_ZXTEST_TEST_REF(Test, AutoRegister));
  ZX_ASSERT_MSG(test_info.name() == "AutoRegister", "TEST registered test with the wrong name.");
  const zxtest::TestInfo& fixture_info =
      runner->GetTestInfo(LIB_ZXTEST_TEST_REF(TestFixture, AutoRegister));
  ZX_ASSERT_MSG(fixture_info.name() == "AutoRegister",
                "TEST_F registered test with the wrong name.");
}

void Register() __attribute__((constructor));
void Register() { zxtest::test::AddCheckFunction(&Verify); }

class ParamTestParent : public zxtest::TestWithParam<int> {};

class ParamTestChild : public ParamTestParent {};

TEST_P(ParamTestChild, BasicTest) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "Aborted test execution with no failures.");
  EXPECT_EQ(2, GetParam());
  TEST_CHECKPOINT();
}
TEST_P(ParamTestChild, SameName) {}

INSTANTIATE_TEST_SUITE_P(SomePrefix, ParamTestChild, ::zxtest::Values(1, 3, 5, 7, 8),
                         [](const auto info) { return "prefix" + std::to_string(info.index); });

enum __enum_type {
  VALUE_1 = 1,
  VALUE_2 = 3,
};

INSTANTIATE_TEST_SUITE_P(Enum, ParamTestChild, ::zxtest::Values(VALUE_1, VALUE_2));

class EnumContainer {
 public:
  enum class Enum {
    kFoo,
    kBar,
  };

  explicit EnumContainer(enum Enum e) : e_(e) {}

  Enum value() { return e_; }

 private:
  enum Enum e_;
};

using EnumTuple = std::tuple<EnumContainer, EnumContainer>;
class EnumTest : public zxtest::TestWithParam<EnumTuple> {};

TEST_P(EnumTest, SomeName) {}

INSTANTIATE_TEST_SUITE_P(EnumClass, EnumTest,
                         ::zxtest::Combine(::zxtest::Values(EnumContainer::Enum::kFoo,
                                                            EnumContainer::Enum::kBar),
                                           ::zxtest::Values(EnumContainer::Enum::kFoo)));

using StringAndBool = std::tuple<std::string, bool>;
class StringAndBoolParent : public zxtest::TestWithParam<StringAndBool> {};
class StringAndBoolChild : public StringAndBoolParent {};

TEST_P(StringAndBoolChild, ThisIsATest) {}
TEST_P(StringAndBoolChild, SameName) {}

INSTANTIATE_TEST_SUITE_P(
    APrefix, StringAndBoolChild,
    ::zxtest::Combine(::zxtest::Values("", "a/b", "/", ".", "..", "../..", "\t", "\r", "ab\n",
                                       std::string("123\0", 4), "\10", "\33", "\177", " ",
                                       "my realm", "~", "`", "!", "@", "$", "%", "^", "&", "*", "(",
                                       ")", "=", "+", "{", "}", "[", "]", "|", "?", ";", "'", "\"",
                                       "<", ">", ",",
                                       "fuchsia-pkg://fuchsia.com/abcd#meta/abcd.cmx"),
                      ::zxtest::Bool()));

class ConstructorTestParent : public zxtest::TestWithParam<int> {
 public:
  ConstructorTestParent(int val) : val_(val) {}

 protected:
  const int val_;
};

class ConstructorTestChild : public ConstructorTestParent {
 public:
  ConstructorTestChild() : ConstructorTestParent(GetParam()) {}
};

TEST_P(ConstructorTestChild, BasicTest) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "Should have passed.");
  EXPECT_EQ(2, val_);
  TEST_CHECKPOINT();
}

INSTANTIATE_TEST_SUITE_P(Prefix, ConstructorTestChild, ::zxtest::Values(2));
}  // namespace
