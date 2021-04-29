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

TEST(HandleTests, GoodHandleRightsTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);
  auto library = WithLibraryZx(R"FIDL(
library example;

using zx;

resource struct MyStruct {
    zx.handle:<THREAD, zx.rights.DUPLICATE | zx.rights.TRANSFER> h;
};
)FIDL",
                               std::move(experimental_flags));
  ASSERT_COMPILED_AND_CONVERT(library);

  auto h_type_ctor = library.LookupStruct("MyStruct")->members[0].type_ctor.get();

  EXPECT_TRUE(h_type_ctor->handle_subtype_identifier.has_value());
  EXPECT_EQ("THREAD", h_type_ctor->handle_subtype_identifier.value().span()->data());

  ASSERT_NOT_NULL(h_type_ctor->type);
  ASSERT_EQ(h_type_ctor->type->kind, fidl::flat::Type::Kind::kHandle);
  auto handle_type = static_cast<const fidl::flat::HandleType*>(h_type_ctor->type);

  EXPECT_EQ(2, handle_type->obj_type);
  EXPECT_EQ(
      static_cast<const fidl::flat::NumericConstantValue<uint32_t>*>(handle_type->rights)->value,
      3);
}

TEST(HandleTests, GoodNoHandleRightsTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  auto library = WithLibraryZx(R"FIDL(
library example;

using zx;

resource struct MyStruct {
    zx.handle:VMO h;
};
)FIDL",
                               std::move(experimental_flags));

  ASSERT_COMPILED_AND_CONVERT(library);

  auto h_type_ctor = library.LookupStruct("MyStruct")->members[0].type_ctor.get();
  ASSERT_NOT_NULL(h_type_ctor->type);
  ASSERT_EQ(h_type_ctor->type->kind, fidl::flat::Type::Kind::kHandle);
  auto handle_type = static_cast<const fidl::flat::HandleType*>(h_type_ctor->type);

  EXPECT_TRUE(h_type_ctor->handle_subtype_identifier.has_value());
  ASSERT_TRUE(h_type_ctor->handle_subtype_identifier.value().span()->data() == "VMO");
  EXPECT_EQ(3, handle_type->obj_type);
  EXPECT_EQ(
      static_cast<const fidl::flat::NumericConstantValue<uint32_t>*>(handle_type->rights)->value,
      fidl::flat::kHandleSameRights);
}

// TODO(fxbug.dev/71536): implement client/server end in the new syntax
TEST(HandleTests, BadInvalidHandleRightsTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  auto library = WithLibraryZx(R"FIDL(
library example;

using zx;

protocol P {
    Method(h zx.handle:<VMO, 1>);  // rights must be zx.rights-typed.
};
)FIDL",
                               std::move(experimental_flags));

  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType,
                                      fidl::ErrCouldNotResolveHandleRights);
}

TEST(HandleTests, BadInvalidHandleRightsTestOld) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  auto library = WithLibraryZx(R"FIDL(
library example;

using zx;

protocol P {
    Method(zx.handle:<VMO, 1> h);  // rights must be zx.rights-typed.
};
)FIDL",
                               std::move(experimental_flags));

  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType,
                                      fidl::ErrCouldNotResolveHandleRights);
}

TEST(HandleTests, GoodPlainHandleTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  auto library = WithLibraryZx(R"FIDL(
library example;

using zx;

resource struct MyStruct {
    zx.handle h;
};
)FIDL",
                               std::move(experimental_flags));
  ASSERT_COMPILED_AND_CONVERT(library);

  auto h_type_ctor = library.LookupStruct("MyStruct")->members[0].type_ctor.get();

  ASSERT_NOT_NULL(h_type_ctor->type);
  ASSERT_EQ(h_type_ctor->type->kind, fidl::flat::Type::Kind::kHandle);
  auto handle_type = static_cast<const fidl::flat::HandleType*>(h_type_ctor->type);

  EXPECT_EQ(0, handle_type->obj_type);
  EXPECT_EQ(
      static_cast<const fidl::flat::NumericConstantValue<uint32_t>*>(handle_type->rights)->value,
      fidl::flat::kHandleSameRights);
}

TEST(HandleTests, GoodHandleFidlDefinedTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  auto library = WithLibraryZx(R"FIDL(
library example;

using zx;

resource struct MyStruct {
  zx.handle:THREAD a;
  zx.handle:<PROCESS> b;
  zx.handle:<VMO, zx.rights.TRANSFER> c;
};
)FIDL",
                               std::move(experimental_flags));

  ASSERT_COMPILED_AND_CONVERT(library);
  auto a = library.LookupStruct("MyStruct")->members[0].type_ctor.get();
  EXPECT_TRUE(a->handle_subtype_identifier.has_value());
  ASSERT_TRUE(a->handle_subtype_identifier.value().span()->data() == "THREAD");
  ASSERT_NOT_NULL(a->type);
  ASSERT_EQ(a->type->kind, fidl::flat::Type::Kind::kHandle);
  auto a_handle_type = static_cast<const fidl::flat::HandleType*>(a->type);
  EXPECT_EQ(2, a_handle_type->obj_type);
  EXPECT_EQ(
      static_cast<const fidl::flat::NumericConstantValue<uint32_t>*>(a_handle_type->rights)->value,
      fidl::flat::kHandleSameRights);

  auto b = library.LookupStruct("MyStruct")->members[1].type_ctor.get();
  EXPECT_TRUE(b->handle_subtype_identifier.has_value());
  ASSERT_TRUE(b->handle_subtype_identifier.value().span()->data() == "PROCESS");
  ASSERT_NOT_NULL(b->type);
  ASSERT_EQ(b->type->kind, fidl::flat::Type::Kind::kHandle);
  auto b_handle_type = static_cast<const fidl::flat::HandleType*>(b->type);
  EXPECT_EQ(1, b_handle_type->obj_type);
  EXPECT_EQ(
      static_cast<const fidl::flat::NumericConstantValue<uint32_t>*>(b_handle_type->rights)->value,
      fidl::flat::kHandleSameRights);

  auto c = library.LookupStruct("MyStruct")->members[2].type_ctor.get();
  EXPECT_TRUE(c->handle_subtype_identifier.has_value());
  ASSERT_TRUE(c->handle_subtype_identifier.value().span()->data() == "VMO");
  ASSERT_NOT_NULL(c->type);
  ASSERT_EQ(c->type->kind, fidl::flat::Type::Kind::kHandle);
  auto c_handle_type = static_cast<const fidl::flat::HandleType*>(c->type);
  EXPECT_EQ(3, c_handle_type->obj_type);
  ASSERT_NOT_NULL(c_handle_type->rights);
  EXPECT_EQ(
      static_cast<const fidl::flat::NumericConstantValue<uint32_t>*>(c_handle_type->rights)->value,
      2);
}

TEST(HandleTests, BadInvalidFidlDefinedHandleSubtype) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  auto library = WithLibraryZx(R"FIDL(
library example;

using zx;

type MyStruct = struct {
  a zx.handle:ZIPPY;
};
)FIDL",
                               std::move(experimental_flags));

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCouldNotResolveHandleSubtype);
  EXPECT_TRUE(library.errors()[0]->msg.find("ZIPPY") != std::string::npos);
}

TEST(HandleTests, BadInvalidFidlDefinedHandleSubtypeOld) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  auto library = WithLibraryZx(R"FIDL(
library example;

using zx;

struct MyStruct {
  zx.handle:ZIPPY a;
};
)FIDL",
                               std::move(experimental_flags));

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCouldNotResolveHandleSubtype);
  EXPECT_TRUE(library.errors()[0]->msg.find("ZIPPY") != std::string::npos);
}

TEST(HandleTests, BadDisallowOldHandlesOld) {
  fidl::ExperimentalFlags experimental_flags;

  auto library = WithLibraryZx(R"FIDL(
library example;

using zx;

struct MyStruct {
    handle<vmo> h;
};
)FIDL",
                               std::move(experimental_flags));

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnknownType);
}

// TODO(fxbug.dev/64629): Consider how we could validate resource_declaration without any use.
TEST(HandleTests, GoodResourceDefinitionOnlySubtypeNoRightsTest) {
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

resource struct MyStruct {
    handle:<VMO> h;
};
)FIDL",
                      std::move(experimental_flags));

  ASSERT_COMPILED_AND_CONVERT(library);

  auto h_type_ctor = library.LookupStruct("MyStruct")->members[0].type_ctor.get();
  ASSERT_NOT_NULL(h_type_ctor->type);
  ASSERT_EQ(h_type_ctor->type->kind, fidl::flat::Type::Kind::kHandle);
  auto handle_type = static_cast<const fidl::flat::HandleType*>(h_type_ctor->type);

  EXPECT_TRUE(h_type_ctor->handle_subtype_identifier.has_value());
  ASSERT_TRUE(h_type_ctor->handle_subtype_identifier.value().span()->data() == "VMO");
  EXPECT_EQ(3, handle_type->obj_type);
  EXPECT_EQ(
      static_cast<const fidl::flat::NumericConstantValue<uint32_t>*>(handle_type->rights)->value,
      fidl::flat::kHandleSameRights);
}

TEST(HandleTests, BadResourceDefinitionMissingRightsPropertyTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

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
    h handle:<VMO, 1>;
};
)FIDL",
                      std::move(experimental_flags));

  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrResourceMissingRightsProperty,
                                      fidl::ErrCouldNotResolveHandleRights);
}

TEST(HandleTests, BadResourceDefinitionMissingRightsPropertyTestOld) {
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

resource struct MyStruct {
    handle:<VMO, 1> h;
};
)FIDL",
                      std::move(experimental_flags));

  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrResourceMissingRightsProperty,
                                      fidl::ErrCouldNotResolveHandleRights);
}

// TODO(fxbug.dev/64629): Consider how we could validate resource_declaration without any use.
TEST(HandleTests, BadResourceDefinitionMissingSubtypePropertyTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

resource_definition handle : uint32 {
    properties {
        rights uint32;
    };
};

type MyStruct = resource struct {
    h handle:VMO;
};
)FIDL",
                      std::move(experimental_flags));

  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrResourceMissingSubtypeProperty,
                                      fidl::ErrCouldNotResolveHandleSubtype);
}

TEST(HandleTests, BadResourceDefinitionMissingSubtypePropertyTestOld) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

resource_definition handle : uint32 {
    properties {
        uint32 rights;
    };
};

resource struct MyStruct {
    handle:VMO h;
};
)FIDL",
                      std::move(experimental_flags));

  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrResourceMissingSubtypeProperty,
                                      fidl::ErrCouldNotResolveHandleSubtype);
}

}  // namespace
