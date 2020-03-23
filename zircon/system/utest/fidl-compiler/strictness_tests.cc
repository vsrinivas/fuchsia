// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>
#include <unittest/unittest.h>

#include "test_library.h"

namespace {

bool invalid_strictness(const std::string& type, const std::string& definition) {
  BEGIN_TEST;

  std::string fidl_library = "library example;\n\n" + definition + "\n";

  TestLibrary library(fidl_library);
  EXPECT_FALSE(library.Compile());

  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  const std::string& expected_error = "cannot specify strictness for \"" + type + "\"";
  ASSERT_STR_STR(errors[0].c_str(), expected_error.c_str());

  END_TEST;
}

bool redundant_strictness(const std::string& strictness, const std::string& definition) {
  // TODO(fxb/7847): Prepending a redundant "strict" qualifier is currently
  // allowed for bits, enums and unions to more easily transition those FIDL
  // types to be flexible by default. This test should be updated and re-enabled
  // when that's done.
  //
  // The original code for this is at
  // <https://fuchsia.git.corp.google.com/fuchsia/+/d66c40c674e1a37fc674c016e76cebd7c8edcd2d/zircon/system/utest/fidl-compiler/strictness_tests.cc#31>.
  return true;
}

bool bits_strictness() {
  BEGIN_TEST;

  TestLibrary library(
      R"FIDL(
library example;

bits StrictFoo {
    BAR = 0x1;
};

flexible bits FlexibleFoo {
    BAR = 0x1;
};

)FIDL",
      fidl::ExperimentalFlags(fidl::ExperimentalFlags::Flag::kFlexibleBitsAndEnums));
  ASSERT_TRUE(library.Compile());
  EXPECT_EQ(library.LookupBits("FlexibleFoo")->strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(library.LookupBits("StrictFoo")->strictness, fidl::types::Strictness::kStrict);

  END_TEST;
}

bool enum_strictness() {
  BEGIN_TEST;

  TestLibrary library(
      R"FIDL(
library example;

enum StrictFoo {
    BAR = 1;
};

flexible enum FlexibleFoo {
    BAR = 1;
};

)FIDL",
      fidl::ExperimentalFlags(fidl::ExperimentalFlags::Flag::kFlexibleBitsAndEnums));
  ASSERT_TRUE(library.Compile());
  EXPECT_EQ(library.LookupEnum("FlexibleFoo")->strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(library.LookupEnum("StrictFoo")->strictness, fidl::types::Strictness::kStrict);

  END_TEST;
}

bool strict_enum_redundant() {
  return redundant_strictness("strict", R"FIDL(
strict enum Foo {
  BAR = 1;
};
)FIDL");
}

bool strict_bits_redundant() {
  return redundant_strictness("strict", R"FIDL(
strict bits Foo {
  BAR = 0x1;
};
)FIDL");
}

bool invalid_strictness_struct() {
  return invalid_strictness("struct", R"FIDL(
strict struct Foo {
    int32 i;
};
)FIDL");
}

bool invalid_strictness_table() {
  return invalid_strictness("table", R"FIDL(
strict table StrictFoo {
};
)FIDL");
}

bool union_strictness() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

union Foo {
    1: int32 i;
};

flexible union FlexibleFoo {
    1: int32 i;
};

strict union StrictFoo {
    1: int32 i;
};

)FIDL");
  ASSERT_TRUE(library.Compile());
  EXPECT_EQ(library.LookupUnion("Foo")->strictness, fidl::types::Strictness::kStrict);
  EXPECT_EQ(library.LookupUnion("FlexibleFoo")->strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(library.LookupUnion("StrictFoo")->strictness, fidl::types::Strictness::kStrict);

  END_TEST;
}

bool strict_union_redundant() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

strict union Foo {
  1: int32 i;
};

)FIDL");
  ASSERT_TRUE(library.Compile());
  ASSERT_EQ(library.LookupUnion("Foo")->strictness, fidl::types::Strictness::kStrict);

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(strictness_tests)
RUN_TEST(bits_strictness);
RUN_TEST(strict_bits_redundant);
RUN_TEST(enum_strictness);
RUN_TEST(union_strictness);
RUN_TEST(strict_enum_redundant);
RUN_TEST(invalid_strictness_table);
RUN_TEST(invalid_strictness_struct);
RUN_TEST(strict_union_redundant);
END_TEST_CASE(strictness_tests)
