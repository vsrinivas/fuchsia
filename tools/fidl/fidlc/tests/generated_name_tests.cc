// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/include/fidl/diagnostics.h"
#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

namespace {

TEST(GeneratedNameTests, GoodInsideStruct) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  bar @generated_name("Good") struct {};
};

)FIDL");
  ASSERT_COMPILED(library);
  auto foo = library.LookupStruct("Foo");
  ASSERT_NOT_NULL(foo);
  auto bar_type = foo->members[0].type_ctor->type;
  EXPECT_EQ(bar_type->name.decl_name(), "Good");
}

TEST(GeneratedNameTests, GoodInsideTable) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = table {
  1: bar @generated_name("Good") struct {};
};

)FIDL");
  ASSERT_COMPILED(library);
  auto foo = library.LookupTable("Foo");
  ASSERT_NOT_NULL(foo);
  auto bar_type = foo->members[0].maybe_used->type_ctor->type;
  EXPECT_EQ(bar_type->name.decl_name(), "Good");
}

TEST(GeneratedNameTests, GoodInsideUnion) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = union {
  1: bar @generated_name("Good") struct {};
};

)FIDL");
  ASSERT_COMPILED(library);
  auto foo = library.LookupUnion("Foo");
  ASSERT_NOT_NULL(foo);
  auto bar_type = foo->members[0].maybe_used->type_ctor->type;
  EXPECT_EQ(bar_type->name.decl_name(), "Good");
}

TEST(GeneratedNameTests, GoodInsideRequest) {
  TestLibrary library(R"FIDL(
library fidl.test;

protocol Foo {
  Bar(@generated_name("Good") struct { x uint32; });
};

)FIDL");
  ASSERT_COMPILED(library);
  auto foo = library.LookupProtocol("Foo");
  ASSERT_NOT_NULL(foo);

  auto id = static_cast<const fidl::flat::IdentifierType*>(foo->methods[0].maybe_request->type);
  auto request_type = static_cast<const fidl::flat::Struct*>(id->type_decl);
  EXPECT_EQ(request_type->name.decl_name(), "Good");
}

TEST(GeneratedNameTests, GoodInsideResponse) {
  TestLibrary library(R"FIDL(
library fidl.test;

protocol Foo {
  Bar() -> (@generated_name("Good") struct { x uint32; });
};

)FIDL");
  ASSERT_COMPILED(library);
  auto foo = library.LookupProtocol("Foo");
  ASSERT_NOT_NULL(foo);

  auto id = static_cast<const fidl::flat::IdentifierType*>(foo->methods[0].maybe_response->type);
  auto response_type = static_cast<const fidl::flat::Struct*>(id->type_decl);
  EXPECT_EQ(response_type->name.decl_name(), "Good");
}

TEST(GeneratedNameTests, GoodInsideResultSuccess) {
  TestLibrary library(R"FIDL(
library fidl.test;

protocol Foo {
  Bar() -> (@generated_name("Good") struct { x uint32; }) error uint32;
};

)FIDL");
  ASSERT_COMPILED(library);
  auto foo = library.LookupProtocol("Foo");
  ASSERT_NOT_NULL(foo);

  auto id = static_cast<const fidl::flat::IdentifierType*>(foo->methods[0].maybe_response->type);
  auto response_type = static_cast<const fidl::flat::Struct*>(id->type_decl);
  auto result_type = response_type->members[0].type_ctor->type;
  auto* result_union = library.LookupUnion(std::string(result_type->name.decl_name()));
  auto* success_type = result_union->members[0].maybe_used->type_ctor->type;
  EXPECT_EQ(success_type->name.decl_name(), "Good");
}

TEST(GeneratedNameTests, GoodInsideResultError) {
  TestLibrary library(R"FIDL(
library fidl.test;

protocol Foo {
  Bar() -> (struct {}) error @generated_name("Good") enum { A = 1; };
};

)FIDL");
  ASSERT_COMPILED(library)
  auto foo = library.LookupProtocol("Foo");
  ASSERT_NOT_NULL(foo);

  auto id = static_cast<const fidl::flat::IdentifierType*>(foo->methods[0].maybe_response->type);
  auto response_type = static_cast<const fidl::flat::Struct*>(id->type_decl);
  auto result_type = response_type->members[0].type_ctor->type;
  auto* result_union = library.LookupUnion(std::string(result_type->name.decl_name()));
  auto* error_type = result_union->members[1].maybe_used->type_ctor->type;
  EXPECT_EQ(error_type->name.decl_name(), "Good");
}

TEST(GeneratedNameTests, GoodOnBits) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  bar @generated_name("Good") bits {
    A = 1;
  };
};

)FIDL");
  ASSERT_COMPILED(library);
  auto foo = library.LookupStruct("Foo");
  ASSERT_NOT_NULL(foo);
  auto bar_type = foo->members[0].type_ctor->type;
  EXPECT_EQ(bar_type->name.decl_name(), "Good");
}

TEST(GeneratedNameTests, GoodOnEnum) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  bar @generated_name("Good") enum {
    A = 1;
  };
};

)FIDL");
  ASSERT_COMPILED(library);
  auto foo = library.LookupStruct("Foo");
  ASSERT_NOT_NULL(foo);
  auto bar_type = foo->members[0].type_ctor->type;
  EXPECT_EQ(bar_type->name.decl_name(), "Good");
}

TEST(GeneratedNameTests, GoodOnStruct) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  bar @generated_name("Good") struct {
    x uint32;
  };
};

)FIDL");
  ASSERT_COMPILED(library);
  auto foo = library.LookupStruct("Foo");
  ASSERT_NOT_NULL(foo);
  auto bar_type = foo->members[0].type_ctor->type;
  EXPECT_EQ(bar_type->name.decl_name(), "Good");
}

TEST(GeneratedNameTests, GoodOnTable) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  bar @generated_name("Good") table {
    1: x uint32;
  };
};

)FIDL");
  ASSERT_COMPILED(library);
  auto foo = library.LookupStruct("Foo");
  ASSERT_NOT_NULL(foo);
  auto bar_type = foo->members[0].type_ctor->type;
  EXPECT_EQ(bar_type->name.decl_name(), "Good");
}

TEST(GeneratedNameTests, GoodOnUnion) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  bar @generated_name("Good") union {
    1: x uint32;
  };
};

)FIDL");
  ASSERT_COMPILED(library);
  auto foo = library.LookupStruct("Foo");
  ASSERT_NOT_NULL(foo);
  auto bar_type = foo->members[0].type_ctor->type;
  EXPECT_EQ(bar_type->name.decl_name(), "Good");
}

TEST(GeneratedNameTests, GoodPreventsCollision) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  foo @generated_name("Bar") struct {};
};

)FIDL");
  ASSERT_COMPILED(library);
}

TEST(GeneratedNameTests, BadOnTypeDeclaration) {
  TestLibrary library(R"FIDL(
library fidl.test;

@generated_name("Good")
type Bad = struct {};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributePlacement);
}

TEST(GeneratedNameTests, BadOnTopLevelStruct) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = @generated_name("Bad") struct {};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributePlacement);
}

TEST(GeneratedNameTests, BadOnIdentifierType) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  bar @generated_name("Bad") Bar;
};

type Bar = struct {};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotAttachAttributeToIdentifier);
}

TEST(GeneratedNameTests, BadOnStructMember) {
  TestLibrary library;
  library.AddFile("bad/fi-0120-b.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributePlacement);
}

TEST(GeneratedNameTests, BadOnEnumMember) {
  TestLibrary library(R"FIDL(
library fidl.test;

type MetaVars = enum {
  FOO = 1;
  @generated_name("BAD")
  BAR = 2;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributePlacement);
}

TEST(GeneratedNameTests, BadOnServiceMember) {
  TestLibrary library(R"FIDL(
library fidl.test;

protocol Foo {};

service Bar {
  @generated_name("One")
  bar_one client_end:Foo;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributePlacement);
}

TEST(GeneratedNameTests, BadMissingArgument) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  bad @generated_name struct {};
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMissingRequiredAnonymousAttributeArg);
}

TEST(GeneratedNameTests, BadInvalidIdentifier) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  bad @generated_name("ez$") struct {};
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidGeneratedName);
}

TEST(GeneratedNameTests, BadNameCollision) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  foo @generated_name("Baz") struct {};
};

type Baz = struct {};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameCollision);
}

TEST(GeneratedNameTests, BadNotString) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  bar @generated_name(true) struct {};
};

)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrTypeCannotBeConvertedToType,
                                      fidl::ErrCouldNotResolveAttributeArg);
}

TEST(GeneratedNameTests, BadNonLiteralArgument) {
  TestLibrary library;
  library.AddFile("bad/fi-0133.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAttributeArgRequiresLiteral);
}

}  // namespace
