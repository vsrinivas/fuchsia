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
  ASSERT_ERR(errors[0], fidl::ErrCannotSpecifyModifier);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "strict");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), type.c_str());
}

TEST(StrictnessTests, bad_duplicate_modifier) {
  TestLibrary library(R"FIDL(
library example;

strict union One { 1: bool b; };
strict strict union Two { 1: bool b; };          // line 5
strict strict strict union Three { 1: bool b; }; // line 6
  )FIDL");
  ASSERT_FALSE(library.Compile());

  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 3);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateModifier);
  EXPECT_EQ(errors[0]->span->position().line, 5);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "strict");
  ASSERT_ERR(errors[1], fidl::ErrDuplicateModifier);
  EXPECT_EQ(errors[1]->span->position().line, 6);
  ASSERT_SUBSTR(errors[1]->msg.c_str(), "strict");
  ASSERT_ERR(errors[2], fidl::ErrDuplicateModifier);
  EXPECT_EQ(errors[2]->span->position().line, 6);
  ASSERT_SUBSTR(errors[2]->msg.c_str(), "strict");
}

TEST(StrictnessTests, bad_conflicting_modifiers) {
  TestLibrary library(R"FIDL(
library example;

strict flexible union SF { 1: bool b; }; // line 4
flexible strict union FS { 1: bool b; }; // line 5
  )FIDL");
  ASSERT_FALSE(library.Compile());

  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrConflictingModifier);
  EXPECT_EQ(errors[0]->span->position().line, 4);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "strict");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "flexible");
  ASSERT_ERR(errors[1], fidl::ErrConflictingModifier);
  EXPECT_EQ(errors[1]->span->position().line, 5);
  ASSERT_SUBSTR(errors[1]->msg.c_str(), "strict");
  ASSERT_SUBSTR(errors[1]->msg.c_str(), "flexible");
}

TEST(StrictnessTests, bits_strictness) {
  TestLibrary library(
      R"FIDL(
library example;

bits DefaultStrictFoo {
    BAR = 0x1;
};

strict bits StrictFoo {
    BAR = 0x1;
};

flexible bits FlexibleFoo {
    BAR = 0x1;
};

)FIDL");
  ASSERT_TRUE(library.Compile());
  EXPECT_EQ(library.LookupBits("FlexibleFoo")->strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(library.LookupBits("StrictFoo")->strictness, fidl::types::Strictness::kStrict);
  EXPECT_EQ(library.LookupBits("DefaultStrictFoo")->strictness, fidl::types::Strictness::kStrict);
}

TEST(StrictnessTests, enum_strictness) {
  TestLibrary library(
      R"FIDL(
library example;

enum DefaultStrictFoo {
    BAR = 1;
};

strict enum StrictFoo {
    BAR = 1;
};

flexible enum FlexibleFoo {
    BAR = 1;
};

)FIDL");
  ASSERT_TRUE(library.Compile());
  EXPECT_EQ(library.LookupEnum("FlexibleFoo")->strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(library.LookupEnum("StrictFoo")->strictness, fidl::types::Strictness::kStrict);
  EXPECT_EQ(library.LookupEnum("DefaultStrictFoo")->strictness, fidl::types::Strictness::kStrict);
}

TEST(StrictnessTests, flexible_enum_redundant) {
  // TODO(fxbug.dev/7847): Once flexible is the default, we should test that
  // the keyword causes an error because it is redundant.
  TestLibrary library(R"FIDL(
library example;

flexible enum Foo {
  BAR = 1;
};
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(StrictnessTests, flexible_bits_redundant) {
  // TODO(fxbug.dev/7847): Once flexible is the default, we should test that
  // the keyword causes an error because it is redundant.
  TestLibrary library(R"FIDL(
library example;

flexible bits Foo {
  BAR = 0x1;
};
)FIDL");
  ASSERT_TRUE(library.Compile());
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
