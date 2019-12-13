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
  BEGIN_TEST;

  std::string fidl_library = "library example;\n\n" + definition + "\n";

  TestLibrary library(fidl_library);
  EXPECT_FALSE(library.Compile());

  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  const std::string& expected_error =
      strictness + " by default, please remove the \"" + strictness + "\"" + " qualifier";
  ASSERT_STR_STR(errors[0].c_str(), expected_error.c_str());

  END_TEST;
}

bool bits_strictness() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

bits StrictFoo {
    BAR = 0x1;
};

experimental_flexible bits FlexibleFoo {
    BAR = 0x1;
};

)FIDL");
  ASSERT_TRUE(library.Compile());
  EXPECT_EQ(library.LookupBits("FlexibleFoo")->strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(library.LookupBits("StrictFoo")->strictness, fidl::types::Strictness::kStrict);

  END_TEST;
}

bool enum_strictness() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

enum StrictFoo {
    BAR = 1;
};

experimental_flexible enum FlexibleFoo {
    BAR = 1;
};

)FIDL");
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

bool invalid_strictness_union() {
  return invalid_strictness("union", R"FIDL(
strict union Foo {
    int32 i;
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

bool xunion_strictness() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

xunion FlexibleFoo {
    1: int32 i;
};

strict xunion StrictFoo {
    1: int32 i;
};

)FIDL");
  ASSERT_TRUE(library.Compile());
  EXPECT_EQ(library.LookupXUnion("FlexibleFoo")->strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(library.LookupXUnion("StrictFoo")->strictness, fidl::types::Strictness::kStrict);

  END_TEST;
}

bool flexible_xunion_redundant() {
  return redundant_strictness("flexible", R"FIDL(
experimental_flexible xunion Foo {
  1: int32 i;
};
)FIDL");
}

}  // namespace

BEGIN_TEST_CASE(strictness_tests)
RUN_TEST(bits_strictness);
RUN_TEST(strict_bits_redundant);
RUN_TEST(enum_strictness);
RUN_TEST(strict_enum_redundant);
RUN_TEST(invalid_strictness_table);
RUN_TEST(invalid_strictness_union);
RUN_TEST(invalid_strictness_struct);
RUN_TEST(xunion_strictness);
RUN_TEST(flexible_xunion_redundant);
END_TEST_CASE(strictness_tests)
