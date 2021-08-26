// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <locale.h>

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/raw_ast.h>
#include <fidl/source_file.h>
#include <zxtest/zxtest.h>

#include "error_test.h"
#include "fidl/diagnostics.h"
#include "fidl/experimental_flags.h"
#include "test_library.h"

namespace {

using fidl::flat::GetType;

TEST(HandleTests, GoodHandleRightsTest) {
  fidl::ExperimentalFlags experimental_flags;
  auto library = WithLibraryZx(R"FIDL(
library example;

using zx;

resource struct MyStruct {
    zx.handle:<THREAD, zx.rights.DUPLICATE | zx.rights.TRANSFER> h;
};
)FIDL",
                               std::move(experimental_flags));
  ASSERT_COMPILED_AND_CONVERT(library);

  const auto& h_type_ctor = library.LookupStruct("MyStruct")->members[0].type_ctor;

  std::visit(fidl::utils::matchers{[&](const std::unique_ptr<fidl::flat::TypeConstructorOld>& t) {
                                     EXPECT_TRUE(t->handle_subtype_identifier.has_value());
                                     EXPECT_EQ("THREAD",
                                               t->handle_subtype_identifier.value().span()->data());
                                   },
                                   [](const std::unique_ptr<fidl::flat::TypeConstructorNew>& t) {
                                     assert(false && "uncoverted copy should be used");
                                   }},
             h_type_ctor);

  auto h_type = GetType(h_type_ctor);
  ASSERT_NOT_NULL(h_type);
  ASSERT_EQ(h_type->kind, fidl::flat::Type::Kind::kHandle);
  auto handle_type = static_cast<const fidl::flat::HandleType*>(h_type);

  EXPECT_EQ(2, handle_type->obj_type);
  EXPECT_EQ(
      static_cast<const fidl::flat::NumericConstantValue<uint32_t>*>(handle_type->rights)->value,
      3);
}

TEST(HandleTests, GoodNoHandleRightsTest) {
  fidl::ExperimentalFlags experimental_flags;

  auto library = WithLibraryZx(R"FIDL(
library example;

using zx;

resource struct MyStruct {
    zx.handle:VMO h;
};
)FIDL",
                               std::move(experimental_flags));

  ASSERT_COMPILED_AND_CONVERT(library);

  const auto& h_type_ctor = library.LookupStruct("MyStruct")->members[0].type_ctor;
  auto h_type = GetType(h_type_ctor);
  ASSERT_NOT_NULL(h_type);
  ASSERT_EQ(h_type->kind, fidl::flat::Type::Kind::kHandle);
  auto handle_type = static_cast<const fidl::flat::HandleType*>(h_type);

  std::visit(fidl::utils::matchers{
                 [&](const std::unique_ptr<fidl::flat::TypeConstructorOld>& t) {
                   EXPECT_TRUE(t->handle_subtype_identifier.has_value());
                   ASSERT_TRUE(t->handle_subtype_identifier.value().span()->data() == "VMO");
                 },
                 [](const std::unique_ptr<fidl::flat::TypeConstructorNew>& t) {
                   assert(false && "uncoverted copy should be used");
                 }},
             h_type_ctor);
  EXPECT_EQ(3, handle_type->obj_type);
  EXPECT_EQ(
      static_cast<const fidl::flat::NumericConstantValue<uint32_t>*>(handle_type->rights)->value,
      fidl::flat::kHandleSameRights);
}

TEST(HandleTests, BadInvalidHandleRightsTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  auto library = WithLibraryZx(R"FIDL(
library example;

using zx;

protocol P {
    Method(struct { h zx.handle:<VMO, 1>; });  // rights must be zx.rights-typed.
};
)FIDL",
                               std::move(experimental_flags));

  // NOTE(fxbug.dev/72924): we provide a more general error because there are multiple
  // possible interpretations.
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType,
                                      fidl::ErrUnexpectedConstraint);
}

TEST(HandleTests, GoodPlainHandleTest) {
  fidl::ExperimentalFlags experimental_flags;

  auto library = WithLibraryZx(R"FIDL(
library example;

using zx;

resource struct MyStruct {
    zx.handle h;
};
)FIDL",
                               std::move(experimental_flags));
  ASSERT_COMPILED_AND_CONVERT(library);

  const auto& h_type_ctor = library.LookupStruct("MyStruct")->members[0].type_ctor;

  auto h_type = GetType(h_type_ctor);
  ASSERT_NOT_NULL(h_type);
  ASSERT_EQ(h_type->kind, fidl::flat::Type::Kind::kHandle);
  auto handle_type = static_cast<const fidl::flat::HandleType*>(h_type);

  EXPECT_EQ(0, handle_type->obj_type);
  EXPECT_EQ(
      static_cast<const fidl::flat::NumericConstantValue<uint32_t>*>(handle_type->rights)->value,
      fidl::flat::kHandleSameRights);
}

TEST(HandleTests, GoodHandleFidlDefinedTest) {
  fidl::ExperimentalFlags experimental_flags;

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
  const auto& a = library.LookupStruct("MyStruct")->members[0].type_ctor;
  auto a_type = GetType(a);
  ASSERT_NOT_NULL(a_type);
  ASSERT_EQ(a_type->kind, fidl::flat::Type::Kind::kHandle);
  auto a_handle_type = static_cast<const fidl::flat::HandleType*>(a_type);
  EXPECT_EQ(2, a_handle_type->obj_type);
  EXPECT_EQ(static_cast<const fidl::flat::HandleRights*>(a_handle_type->rights)->value,
            fidl::flat::kHandleSameRights);

  const auto& b = library.LookupStruct("MyStruct")->members[1].type_ctor;
  auto b_type = GetType(b);
  ASSERT_NOT_NULL(b_type);
  ASSERT_EQ(b_type->kind, fidl::flat::Type::Kind::kHandle);
  auto b_handle_type = static_cast<const fidl::flat::HandleType*>(b_type);
  EXPECT_EQ(1, b_handle_type->obj_type);
  EXPECT_EQ(static_cast<const fidl::flat::HandleRights*>(b_handle_type->rights)->value,
            fidl::flat::kHandleSameRights);

  const auto& c = library.LookupStruct("MyStruct")->members[2].type_ctor;
  auto c_type = GetType(c);
  ASSERT_NOT_NULL(c_type);
  ASSERT_EQ(c_type->kind, fidl::flat::Type::Kind::kHandle);
  auto c_handle_type = static_cast<const fidl::flat::HandleType*>(c_type);
  EXPECT_EQ(3, c_handle_type->obj_type);
  ASSERT_NOT_NULL(c_handle_type->rights);
  EXPECT_EQ(static_cast<const fidl::flat::HandleRights*>(c_handle_type->rights)->value, 2);
}

TEST(HandleTests, BadInvalidFidlDefinedHandleSubtype) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  auto library = WithLibraryZx(R"FIDL(
library example;

using zx;

type MyStruct = struct {
  a zx.handle:ZIPPY;
};
)FIDL",
                               std::move(experimental_flags));

  // NOTE(fxbug.dev/72924): we provide a more general error because there are multiple
  // possible interpretations.
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedConstraint);
}

TEST(HandleTests, BadDisallowOldHandles) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  auto library = WithLibraryZx(R"FIDL(
library example;

using zx;

type MyStruct = struct {
    h handle<vmo>;
};
)FIDL",
                               experimental_flags);
  // TODO(fxbug.dev/77101): provide a less confusing error
  // NOTE(fxbug.dev/72924): the old syntax returns a different error because
  // it tries to resolve the parameters before checking that handle points to
  // a resource definition
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrHandleNotResource);
}

// TODO(fxbug.dev/64629): Consider how we could validate resource_declaration without any use.
TEST(HandleTests, GoodResourceDefinitionOnlySubtypeNoRightsTest) {
  fidl::ExperimentalFlags experimental_flags;

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

  const auto& h_type_ctor = library.LookupStruct("MyStruct")->members[0].type_ctor;
  auto h_type = GetType(h_type_ctor);
  ASSERT_NOT_NULL(h_type);
  ASSERT_EQ(h_type->kind, fidl::flat::Type::Kind::kHandle);
  auto handle_type = static_cast<const fidl::flat::HandleType*>(h_type);

  std::visit(fidl::utils::matchers{
                 [&](const std::unique_ptr<fidl::flat::TypeConstructorOld>& t) {
                   EXPECT_TRUE(t->handle_subtype_identifier.has_value());
                   ASSERT_TRUE(t->handle_subtype_identifier.value().span()->data() == "VMO");
                 },
                 [](const std::unique_ptr<fidl::flat::TypeConstructorNew>& t) {
                   assert(false && "uncoverted copy should be used");
                 }},
             h_type_ctor);
  EXPECT_EQ(3, handle_type->obj_type);
  EXPECT_EQ(
      static_cast<const fidl::flat::NumericConstantValue<uint32_t>*>(handle_type->rights)->value,
      fidl::flat::kHandleSameRights);
}

// TODO(fxbug.dev/64629): Consider how we could validate resource_declaration without any use.
TEST(HandleTests, BadResourceDefinitionMissingRightsPropertyTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

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

  // TODO(fxbug.dev/75112): should include ErrResourceMissingRightsProperty
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedConstraint);
}

// TODO(fxbug.dev/64629): Consider how we could validate resource_declaration without any use.
TEST(HandleTests, BadResourceDefinitionMissingSubtypePropertyTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

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

  // TODO(fxbug.dev/75112): should include ErrResourceMissingSubtypeProperty
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedConstraint);
}

// TODO(fxbug.dev/64629): Consider how we could validate resource_declaration without any use.
TEST(HandleTests, BadResourceSubtypeNotEnum) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;

type obj_type = struct {};

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

  // TODO(fxbug.dev/75112): should include ErrResourceSubtypePropertyMustReferToEnum
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedConstraint);
}

TEST(HandleTests, BadNonIdentifierSubtype) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

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
)FIDL",
                      experimental_flags);

  // TODO(fxbug.dev/75112): should include ErrHandleSubtypeMustReferToResourceSubtype
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedConstraint);
}

// TODO(fxbug.dev/64629): Consider how we could validate resource_declaration without any use.
TEST(HandleTests, BadResourceDefinitionNonBitsRights) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;

type obj_type = enum : uint32 {
    NONE = 0;
    VMO = 3;
};

resource_definition handle : uint32 {
    properties {
        subtype obj_type;
        rights string;
    };
};

type MyStruct = resource struct {
    h handle:<VMO, "hello">;
};
)FIDL",
                      experimental_flags);

  // TODO(fxbug.dev/75112): should include ErrResourceMissingSubtypeProperty
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedConstraint);
}

TEST(HandleTests, BadBareHandleNoConstraints) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;

type MyStruct = resource struct {
    h handle;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrHandleNotResource);
}

TEST(HandleTests, BadBareHandleWithConstraints) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type MyStruct = resource struct {
    h handle:VMO;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrHandleNotResource);
}

TEST(HandleTests, BadBareHandleWithConstraintsThroughAlias) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

alias my_handle = handle;

type MyStruct = resource struct {
    h my_handle:VMO;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrHandleNotResource);
}

}  // namespace
