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

#include "error_test.h"
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

bool invalid_handle_rights_test() {
  BEGIN_TEST;

  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

protocol P {
    Method(handle<vmo, 4294967296> h);  // uint32 max + 1
};
)FIDL",
                      std::move(experimental_flags));

  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_ERR(errors[1], fidl::ErrCouldNotResolveHandleRights);

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

bool handle_fidl_defined_test() {
  BEGIN_TEST;

  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

enum obj_type : uint32 {
    NONE = 0;
    PROCESS = 1;
    THREAD = 2;
    VMO = 3;
};

resource handle : uint32 {
    properties {
        obj_type subtype;
    };
};

struct MyStruct {
  handle:THREAD a;
  // TODO(fxbug.dev/51001): Parse with <>, e.g. handle:<PROCESS> b;
  // TODO(fxbug.dev/51001): Parse with <> and rights, e.g. handle:<VMO, 1> c;
};
)FIDL",
                      std::move(experimental_flags));

  EXPECT_TRUE(library.Compile());
  auto a = library.LookupStruct("MyStruct")->members[0].type_ctor.get();
  EXPECT_FALSE(a->handle_subtype.has_value());
  EXPECT_TRUE(a->handle_subtype_identifier.has_value());
  ASSERT_TRUE(a->handle_subtype_identifier.value().span()->data() == "THREAD");
  ASSERT_NULL(a->handle_rights);

  END_TEST;
}

bool invalid_fidl_defined_handle_subtype() {
  BEGIN_TEST;

  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

enum obj_type : uint32 {
    NONE = 0;
};

resource handle : uint32 {
    properties {
        obj_type subtype;
    };
};

struct MyStruct {
  handle:ZIPPY a;
};
)FIDL",
                      std::move(experimental_flags));

  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrCouldNotResolveHandleSubtype);
  EXPECT_TRUE(errors[0]->msg.find("ZIPPY") != std::string::npos);

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(handle_tests)
RUN_TEST(handle_rights_test)
RUN_TEST(no_handle_rights_test)
RUN_TEST(invalid_handle_rights_test)
RUN_TEST(plain_handle_test)
RUN_TEST(handle_fidl_defined_test)
RUN_TEST(invalid_fidl_defined_handle_subtype)
END_TEST_CASE(handle_tests)
