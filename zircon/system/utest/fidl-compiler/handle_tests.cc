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
#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

TEST(HandleTests, handle_rights_test) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

enum obj_type : uint32 {
    NONE = 0;
    VMO = 3;
};

resource_definition handle : uint32 {
    properties {
        obj_type subtype;
    };
};

struct MyStruct {
    handle:<VMO, 1> h;
};
)FIDL",
                      std::move(experimental_flags));

  EXPECT_TRUE(library.Compile());

  auto h_type_ctor = library.LookupStruct("MyStruct")->members[0].type_ctor.get();

  EXPECT_FALSE(h_type_ctor->handle_subtype.has_value());
  EXPECT_TRUE(h_type_ctor->handle_subtype_identifier.has_value());
  ASSERT_TRUE(h_type_ctor->handle_subtype_identifier.value().span()->data() == "VMO");
  ASSERT_NOT_NULL(h_type_ctor->handle_rights);

  ASSERT_EQ(static_cast<const fidl::flat::NumericConstantValue<uint32_t>&>(
                h_type_ctor->handle_rights->Value())
                .value,
            1);
}

TEST(HandleTests, no_handle_rights_test) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

enum obj_type : uint32 {
    NONE = 0;
    VMO = 3;
};

resource_definition handle : uint32 {
    properties {
        obj_type subtype;
    };
};

struct MyStruct {
    handle:VMO h;
};
)FIDL",
                      std::move(experimental_flags));

  EXPECT_TRUE(library.Compile());

  auto h_type_ctor = library.LookupStruct("MyStruct")->members[0].type_ctor.get();

  EXPECT_FALSE(h_type_ctor->handle_subtype.has_value());
  EXPECT_TRUE(h_type_ctor->handle_subtype_identifier.has_value());
  ASSERT_TRUE(h_type_ctor->handle_subtype_identifier.value().span()->data() == "VMO");
  ASSERT_NULL(h_type_ctor->handle_rights);
}

TEST(HandleTests, invalid_handle_rights_test) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

enum obj_type : uint32 {
    NONE = 0;
    VMO = 3;
};

resource_definition handle : uint32 {
    properties {
        obj_type subtype;
    };
};

protocol P {
    Method(handle:<VMO, 4294967296> h);  // uint32 max + 1
};
)FIDL",
                      std::move(experimental_flags));

  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_ERR(errors[1], fidl::ErrCouldNotResolveHandleRights);
}

TEST(HandleTests, plain_handle_test) {
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
}

TEST(HandleTests, handle_fidl_defined_test) {
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

resource_definition handle : uint32 {
    properties {
        obj_type subtype;
    };
};

struct MyStruct {
  handle:THREAD a;
  handle:<PROCESS> b;
  handle:<VMO, 45> c;
};
)FIDL",
                      std::move(experimental_flags));

  EXPECT_TRUE(library.Compile());
  auto a = library.LookupStruct("MyStruct")->members[0].type_ctor.get();
  EXPECT_FALSE(a->handle_subtype.has_value());
  EXPECT_TRUE(a->handle_subtype_identifier.has_value());
  ASSERT_TRUE(a->handle_subtype_identifier.value().span()->data() == "THREAD");
  ASSERT_NULL(a->handle_rights);

  auto b = library.LookupStruct("MyStruct")->members[1].type_ctor.get();
  EXPECT_FALSE(b->handle_subtype.has_value());
  EXPECT_TRUE(b->handle_subtype_identifier.has_value());
  ASSERT_TRUE(b->handle_subtype_identifier.value().span()->data() == "PROCESS");
  ASSERT_NULL(b->handle_rights);

  auto c = library.LookupStruct("MyStruct")->members[2].type_ctor.get();
  EXPECT_FALSE(c->handle_subtype.has_value());
  EXPECT_TRUE(c->handle_subtype_identifier.has_value());
  ASSERT_TRUE(c->handle_subtype_identifier.value().span()->data() == "VMO");
  ASSERT_NOT_NULL(c->handle_rights);
  ASSERT_EQ(
      static_cast<const fidl::flat::NumericConstantValue<uint32_t>&>(c->handle_rights->Value())
          .value,
      45);
}

TEST(HandleTests, invalid_fidl_defined_handle_subtype) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

enum obj_type : uint32 {
    NONE = 0;
};

resource_definition handle : uint32 {
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
}

TEST(HandleTests, disallow_old_handles) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kDisallowOldHandleSyntax);

  TestLibrary library(R"FIDL(
library example;

struct MyStruct {
    handle<vmo> h;
};
)FIDL",
                      std::move(experimental_flags));

  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrOldHandleSyntax);
}

}  // namespace
