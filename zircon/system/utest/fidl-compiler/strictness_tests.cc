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

bool invalid_strict(const std::string& type, const std::string& definition) {
  BEGIN_TEST;

  std::string fidl_library = "library example;\n\n" + definition + "\n";

  TestLibrary library(fidl_library);
  EXPECT_FALSE(library.Compile());

  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  const std::string& expected_error = "\"" + type + "\" cannot be strict";
  ASSERT_STR_STR(errors[0].c_str(), expected_error.c_str());

  END_TEST;
}

bool bits_strictness() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

bits FlexibleFoo {
    BAR = 0x1;
};

strict bits StrictFoo {
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

enum FlexibleFoo {
    BAR = 1;
};

strict enum StrictFoo {
    BAR = 1;
};

)FIDL");
  ASSERT_TRUE(library.Compile());
  EXPECT_EQ(library.LookupEnum("FlexibleFoo")->strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(library.LookupEnum("StrictFoo")->strictness, fidl::types::Strictness::kStrict);

  END_TEST;
}

bool table_strictness() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

table FlexibleFoo {
};

strict table StrictFoo {
};

)FIDL");
  ASSERT_TRUE(library.Compile());
  EXPECT_EQ(library.LookupTable("FlexibleFoo")->strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(library.LookupTable("StrictFoo")->strictness, fidl::types::Strictness::kStrict);

  END_TEST;
}

bool invalid_strict_union() {
  return invalid_strict("union", R"FIDL(
strict union Foo {
    int32 i;
};
)FIDL");
}

bool invalid_strict_struct() {
  return invalid_strict("struct", R"FIDL(
strict struct Foo {
    int32 i;
};
)FIDL");
}

bool xunion_strictness() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

xunion FlexibleFoo {
    int32 i;
};

strict xunion StrictFoo {
    int32 i;
};

)FIDL");
  ASSERT_TRUE(library.Compile());
  EXPECT_EQ(library.LookupXUnion("FlexibleFoo")->strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(library.LookupXUnion("StrictFoo")->strictness, fidl::types::Strictness::kStrict);

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(strictness_tests)
RUN_TEST(bits_strictness);
RUN_TEST(enum_strictness);
RUN_TEST(table_strictness);
RUN_TEST(invalid_strict_union);
RUN_TEST(invalid_strict_struct);
RUN_TEST(xunion_strictness);
END_TEST_CASE(strictness_tests)
