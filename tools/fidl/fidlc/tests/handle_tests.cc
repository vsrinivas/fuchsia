// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <locale.h>

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/include/fidl/diagnostics.h"
#include "tools/fidl/fidlc/include/fidl/flat_ast.h"
#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

namespace {

TEST(HandleTests, GoodHandleRightsTest) {
  TestLibrary library(R"FIDL(
library example;

using zx;

type MyStruct = resource struct {
    h zx.handle:<THREAD, zx.rights.DUPLICATE | zx.rights.TRANSFER>;
};
)FIDL");
  library.UseLibraryZx();
  ASSERT_COMPILED(library);

  const auto& h_type_ctor = library.LookupStruct("MyStruct")->members[0].type_ctor;

  EXPECT_TRUE(h_type_ctor->resolved_params.subtype_raw != nullptr);
  EXPECT_EQ("THREAD", h_type_ctor->resolved_params.subtype_raw->span.data());

  auto h_type = h_type_ctor->type;
  ASSERT_NOT_NULL(h_type);
  ASSERT_EQ(h_type->kind, fidl::flat::Type::Kind::kHandle);
  auto handle_type = static_cast<const fidl::flat::HandleType*>(h_type);

  EXPECT_EQ(2, handle_type->obj_type);
  EXPECT_EQ(
      static_cast<const fidl::flat::NumericConstantValue<uint32_t>*>(handle_type->rights)->value,
      3);
}

TEST(HandleTests, GoodNoHandleRightsTest) {
  TestLibrary library(R"FIDL(
library example;

using zx;

type MyStruct = resource struct {
    h zx.handle:VMO;
};
)FIDL");
  library.UseLibraryZx();

  ASSERT_COMPILED(library);

  const auto& h_type_ctor = library.LookupStruct("MyStruct")->members[0].type_ctor;
  auto h_type = h_type_ctor->type;
  ASSERT_NOT_NULL(h_type);
  ASSERT_EQ(h_type->kind, fidl::flat::Type::Kind::kHandle);
  auto handle_type = static_cast<const fidl::flat::HandleType*>(h_type);

  EXPECT_TRUE(h_type_ctor->resolved_params.subtype_raw != nullptr);
  EXPECT_EQ("VMO", h_type_ctor->resolved_params.subtype_raw->span.data());
  EXPECT_EQ(3, handle_type->obj_type);
  EXPECT_EQ(
      static_cast<const fidl::flat::NumericConstantValue<uint32_t>*>(handle_type->rights)->value,
      fidl::flat::kHandleSameRights);
}

TEST(HandleTests, BadInvalidHandleRightsTest) {
  TestLibrary library(R"FIDL(
library example;

using zx;

protocol P {
    Method(struct { h zx.handle:<VMO, 1>; });  // rights must be zx.rights-typed.
};
)FIDL");
  library.UseLibraryZx();

  // NOTE(fxbug.dev/72924): we provide a more general error because there are multiple
  // possible interpretations.
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrTypeCannotBeConvertedToType,
                                      fidl::ErrUnexpectedConstraint);
}

TEST(HandleTests, GoodPlainHandleTest) {
  TestLibrary library(R"FIDL(
library example;

using zx;

type MyStruct = resource struct {
    h zx.handle;
};
)FIDL");
  library.UseLibraryZx();
  ASSERT_COMPILED(library);

  const auto& h_type_ctor = library.LookupStruct("MyStruct")->members[0].type_ctor;

  auto h_type = h_type_ctor->type;
  ASSERT_NOT_NULL(h_type);
  ASSERT_EQ(h_type->kind, fidl::flat::Type::Kind::kHandle);
  auto handle_type = static_cast<const fidl::flat::HandleType*>(h_type);

  EXPECT_EQ(0, handle_type->obj_type);
  EXPECT_EQ(
      static_cast<const fidl::flat::NumericConstantValue<uint32_t>*>(handle_type->rights)->value,
      fidl::flat::kHandleSameRights);
}

TEST(HandleTests, GoodHandleFidlDefinedTest) {
  TestLibrary library(R"FIDL(
library example;

using zx;

type MyStruct = resource struct {
  a zx.handle:THREAD;
  b zx.handle:<PROCESS>;
  c zx.handle:<VMO, zx.rights.TRANSFER>;
};
)FIDL");
  library.UseLibraryZx();

  ASSERT_COMPILED(library);
  const auto& a = library.LookupStruct("MyStruct")->members[0].type_ctor;
  auto a_type = a->type;
  ASSERT_NOT_NULL(a_type);
  ASSERT_EQ(a_type->kind, fidl::flat::Type::Kind::kHandle);
  auto a_handle_type = static_cast<const fidl::flat::HandleType*>(a_type);
  EXPECT_EQ(2, a_handle_type->obj_type);
  EXPECT_EQ(static_cast<const fidl::flat::HandleRights*>(a_handle_type->rights)->value,
            fidl::flat::kHandleSameRights);

  const auto& b = library.LookupStruct("MyStruct")->members[1].type_ctor;
  auto b_type = b->type;
  ASSERT_NOT_NULL(b_type);
  ASSERT_EQ(b_type->kind, fidl::flat::Type::Kind::kHandle);
  auto b_handle_type = static_cast<const fidl::flat::HandleType*>(b_type);
  EXPECT_EQ(1, b_handle_type->obj_type);
  EXPECT_EQ(static_cast<const fidl::flat::HandleRights*>(b_handle_type->rights)->value,
            fidl::flat::kHandleSameRights);

  const auto& c = library.LookupStruct("MyStruct")->members[2].type_ctor;
  auto c_type = c->type;
  ASSERT_NOT_NULL(c_type);
  ASSERT_EQ(c_type->kind, fidl::flat::Type::Kind::kHandle);
  auto c_handle_type = static_cast<const fidl::flat::HandleType*>(c_type);
  EXPECT_EQ(3, c_handle_type->obj_type);
  ASSERT_NOT_NULL(c_handle_type->rights);
  EXPECT_EQ(static_cast<const fidl::flat::HandleRights*>(c_handle_type->rights)->value, 2);
}

TEST(HandleTests, BadInvalidFidlDefinedHandleSubtype) {
  TestLibrary library(R"FIDL(
library example;

using zx;

type MyStruct = struct {
  a zx.handle:ZIPPY;
};
)FIDL");
  library.UseLibraryZx();

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameNotFound);
}

TEST(HandleTests, BadDisallowOldHandles) {
  TestLibrary library(R"FIDL(
library example;

using zx;

type MyStruct = struct {
    h handle<vmo>;
};
)FIDL");
  library.UseLibraryZx();

  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrNameNotFound, fidl::ErrNameNotFound);
  EXPECT_SUBSTR(library.errors()[0]->msg.c_str(), "cannot find 'handle'");
  EXPECT_SUBSTR(library.errors()[1]->msg.c_str(), "cannot find 'vmo'");
}

TEST(HandleTests, GoodResourceDefinitionOnlySubtypeNoRightsTest) {
  TestLibrary library(R"FIDL(library example;

type obj_type = strict enum : uint32 {
    NONE = 0;
    VMO = 3;
};

resource_definition handle : uint32 {
    properties {
        subtype obj_type;
    };
};

type MyStruct = resource struct {
    h handle:VMO;
};
)FIDL");

  ASSERT_COMPILED(library);

  const auto& h_type_ctor = library.LookupStruct("MyStruct")->members[0].type_ctor;
  auto h_type = h_type_ctor->type;
  ASSERT_NOT_NULL(h_type);
  ASSERT_EQ(h_type->kind, fidl::flat::Type::Kind::kHandle);
  auto handle_type = static_cast<const fidl::flat::HandleType*>(h_type);

  EXPECT_TRUE(h_type_ctor->resolved_params.subtype_raw != nullptr);
  ASSERT_TRUE(h_type_ctor->resolved_params.subtype_raw->span.data() == "VMO");
  EXPECT_EQ(3, handle_type->obj_type);
  EXPECT_EQ(
      static_cast<const fidl::flat::NumericConstantValue<uint32_t>*>(handle_type->rights)->value,
      fidl::flat::kHandleSameRights);
}

TEST(HandleTests, BadInvalidSubtypeAtUseSite) {
  TestLibrary library(R"FIDL(
library example;

type obj_type = enum : uint32 {
    NONE = 0;
    VMO = 3;
};

resource_definition handle : uint32 {
    properties {
        subtype obj_type;
    };
};

type MyStruct = resource struct {
    h handle:<1, optional>;
};
)FIDL");

  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrTypeCannotBeConvertedToType,
                                      fidl::ErrUnexpectedConstraint);
}

TEST(HandleTests, BadInvalidRightsAtUseSite) {
  TestLibrary library(R"FIDL(
library example;

type obj_type = enum : uint32 {
    NONE = 0;
    VMO = 3;
};

resource_definition handle : uint32 {
    properties {
        subtype obj_type;
        rights uint32;
    };
};

type MyStruct = resource struct {
    h handle:<VMO, "my_improperly_typed_rights", optional>;
};
)FIDL");

  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrTypeCannotBeConvertedToType,
                                      fidl::ErrUnexpectedConstraint);
}

TEST(HandleTests, BadBareHandleNoConstraints) {
  TestLibrary library(R"FIDL(
library example;

type MyStruct = resource struct {
    h handle;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameNotFound);
}

TEST(HandleTests, BadBareHandleWithConstraints) {
  TestLibrary library(R"FIDL(
library example;

type MyStruct = resource struct {
    h handle:VMO;
};
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrNameNotFound, fidl::ErrNameNotFound);
}

TEST(HandleTests, BadBareHandleWithConstraintsThroughAlias) {
  TestLibrary library(R"FIDL(
library example;

alias my_handle = handle;

type MyStruct = resource struct {
    h my_handle:VMO;
};
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrNameNotFound, fidl::ErrNameNotFound);
}

}  // namespace
