// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <locale.h>

#include <fidl/attributes.h>
#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/raw_ast.h>
#include <fidl/source_file.h>
#include <unittest/unittest.h>

#include "test_library.h"

namespace {

bool handle_rights_test() {
  BEGIN_TEST;

  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

struct MyStruct {
    handle<vmo, 1> h;
};
)FIDL",
                      std::move(experimental_flags));

  EXPECT_TRUE(library.Compile());

  auto h_type_ctor = library.LookupStruct("MyStruct")->members[0].type_ctor.get();

  ASSERT_EQ(h_type_ctor->handle_subtype.value(), fidl::types::HandleSubtype::kVmo);
  ASSERT_EQ(static_cast<const fidl::flat::NumericConstantValue<uint32_t>&>(
                h_type_ctor->handle_rights->Value())
                .value,
            1);

  END_TEST;
}

bool no_handle_rights_test() {
  BEGIN_TEST;

  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

struct MyStruct {
    handle<vmo> h;
};
)FIDL",
                      std::move(experimental_flags));

  EXPECT_TRUE(library.Compile());

  auto h_type_ctor = library.LookupStruct("MyStruct")->members[0].type_ctor.get();

  ASSERT_EQ(h_type_ctor->handle_subtype.value(), fidl::types::HandleSubtype::kVmo);
  ASSERT_NULL(h_type_ctor->handle_rights);

  END_TEST;
}

bool plain_handle_test() {
  BEGIN_TEST;

  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

struct MyStruct {
    handle h;
};
)FIDL",
                      std::move(experimental_flags));

  EXPECT_TRUE(library.Compile());

  auto h_type_ctor = library.LookupStruct("MyStruct")->members[0].type_ctor.get();

  EXPECT_FALSE(h_type_ctor->handle_subtype.has_value());
  ASSERT_NULL(h_type_ctor->handle_rights);

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(handle_tests)
RUN_TEST(handle_rights_test)
RUN_TEST(no_handle_rights_test)
RUN_TEST(plain_handle_test)
END_TEST_CASE(handle_tests)
