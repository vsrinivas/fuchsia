// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>
#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

void invalid_strictness(const std::string& type, const std::string& definition) {
  std::string fidl_library = "library example;\n\n" + definition + "\n";

  TestLibrary library(fidl_library);
  EXPECT_FALSE(library.Compile());

  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrCannotSpecifyStrict);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), type.c_str());
}

void redundant_strictness(const std::string& strictness, const std::string& definition) {
  // TODO(fxbug.dev/7847): Prepending a redundant "strict" qualifier is currently
  // allowed for bits, enums and unions to more easily transition those FIDL
  // types to be flexible by default. This test should be updated and re-enabled
  // when that's done.
  //
  // The original code for this is at
  // <https://fuchsia.git.corp.google.com/fuchsia/+/d66c40c674e1a37fc674c016e76cebd7c8edcd2d/zircon/system/utest/fidl-compiler/strictness_tests.cc#31>.
}

TEST(StrictnessTests, bits_strictness) {
  TestLibrary library(
      R"FIDL(
library example;

bits StrictFoo {
    BAR = 0x1;
};

flexible bits FlexibleFoo {
    BAR = 0x1;
};

)FIDL");
  ASSERT_TRUE(library.Compile());
  EXPECT_EQ(library.LookupBits("FlexibleFoo")->strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(library.LookupBits("StrictFoo")->strictness, fidl::types::Strictness::kStrict);
}

TEST(StrictnessTests, enum_strictness) {
  TestLibrary library(
      R"FIDL(
library example;

enum StrictFoo {
    BAR = 1;
};

flexible enum FlexibleFoo {
    BAR = 1;
};

)FIDL");
  ASSERT_TRUE(library.Compile());
  EXPECT_EQ(library.LookupEnum("FlexibleFoo")->strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(library.LookupEnum("StrictFoo")->strictness, fidl::types::Strictness::kStrict);
}

TEST(StrictnessTests, strict_enum_redundant) {
  redundant_strictness("strict", R"FIDL(
strict enum Foo {
  BAR = 1;
};
)FIDL");
}

TEST(StrictnessTests, strict_bits_redundant) {
  redundant_strictness("strict", R"FIDL(
strict bits Foo {
  BAR = 0x1;
};
)FIDL");
}

TEST(StrictnessTests, invalid_strictness_struct) {
  invalid_strictness("struct", R"FIDL(
strict struct Foo {
    int32 i;
};
)FIDL");
}

TEST(StrictnessTests, invalid_strictness_table) {
  invalid_strictness("table", R"FIDL(
strict table StrictFoo {
};
)FIDL");
}

TEST(StrictnessTests, union_strictness) {
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
}

TEST(StrictnessTests, strict_union_redundant) {
  TestLibrary library(R"FIDL(
library example;

strict union Foo {
  1: int32 i;
};

)FIDL");
  ASSERT_TRUE(library.Compile());
  ASSERT_EQ(library.LookupUnion("Foo")->strictness, fidl::types::Strictness::kStrict);
}

}  // namespace
