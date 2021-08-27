// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/names.h>
#include <zxtest/zxtest.h>

#include "error_test.h"
#include "fidl/diagnostics.h"
#include "fidl/experimental_flags.h"
#include "test_library.h"

namespace {

using fidl::flat::GetLayoutInvocation;
using fidl::flat::GetType;

TEST(AliasTests, BadDuplicateAlias) {
  TestLibrary library(R"FIDL(
library example;

type Message = struct {
    f alias_of_int16;
};

alias alias_of_int16 = int16;
alias alias_of_int16 = int16;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameCollision);
}

TEST(AliasTests, GoodPrimitive) {
  TestLibrary library(R"FIDL(library example;

type Message = struct {
    f alias_of_int16;
};

alias alias_of_int16 = int16;
)FIDL");
  ASSERT_COMPILED(library);
  auto msg = library.LookupStruct("Message");
  ASSERT_NOT_NULL(msg);
  ASSERT_EQ(msg->members.size(), 1);

  auto type = GetType(msg->members[0].type_ctor);
  ASSERT_EQ(type->kind, fidl::flat::Type::Kind::kPrimitive);
  ASSERT_EQ(type->nullability, fidl::types::Nullability::kNonnullable);

  auto primitive_type = static_cast<const fidl::flat::PrimitiveType*>(type);
  ASSERT_EQ(primitive_type->subtype, fidl::types::PrimitiveSubtype::kInt16);

  auto invocation = GetLayoutInvocation(msg->members[0].type_ctor);
  ASSERT_NOT_NULL(invocation.from_type_alias);
  EXPECT_STR_EQ(fidl::NameFlatName(invocation.from_type_alias->name).c_str(),
                "example/alias_of_int16");
  EXPECT_NULL(invocation.element_type_resolved);
  EXPECT_NULL(invocation.size_resolved);
  EXPECT_EQ(invocation.nullability, fidl::types::Nullability::kNonnullable);
}

TEST(AliasTests, GoodPrimitiveTypeAliasBeforeUse) {
  TestLibrary library(R"FIDL(library example;

alias alias_of_int16 = int16;

type Message = struct {
    f alias_of_int16;
};
)FIDL");
  ASSERT_COMPILED(library);
  auto msg = library.LookupStruct("Message");
  ASSERT_NOT_NULL(msg);
  ASSERT_EQ(msg->members.size(), 1);

  auto type = GetType(msg->members[0].type_ctor);
  ASSERT_EQ(type->kind, fidl::flat::Type::Kind::kPrimitive);
  ASSERT_EQ(type->nullability, fidl::types::Nullability::kNonnullable);

  auto primitive_type = static_cast<const fidl::flat::PrimitiveType*>(type);
  ASSERT_EQ(primitive_type->subtype, fidl::types::PrimitiveSubtype::kInt16);

  auto invocation = GetLayoutInvocation(msg->members[0].type_ctor);
  ASSERT_NOT_NULL(invocation.from_type_alias);
  EXPECT_STR_EQ(fidl::NameFlatName(invocation.from_type_alias->name).c_str(),
                "example/alias_of_int16");
  EXPECT_NULL(invocation.element_type_resolved);
  EXPECT_NULL(invocation.size_resolved);
  EXPECT_EQ(invocation.nullability, fidl::types::Nullability::kNonnullable);
}

TEST(AliasTests, BadPrimitiveTypeShadowing) {
  TestLibrary library(R"FIDL(
library example;

alias uint32 = uint32;

type Message = struct {
    f uint32;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrIncludeCycle);
}

TEST(AliasTests, BadNoOptionalOnPrimitive) {
  TestLibrary library(R"FIDL(
library test.optionals;

type Bad = struct {
    opt_num int64:optional;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBeNullable);
}

TEST(AliasTests, BadMultipleConstraintsOnPrimitive) {
  TestLibrary library(R"FIDL(
library test.optionals;

type Bad = struct {
    opt_num int64:<optional, foo, bar>;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTooManyConstraints);
}

TEST(AliasTests, BadNoOptionalOnAliasedPrimitive) {
  TestLibrary library(R"FIDL(
library test.optionals;

alias alias = int64;

type Bad = struct {
    opt_num alias:optional;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBeNullable);
}

TEST(AliasTests, GoodVectorParameterizedOnDecl) {
  TestLibrary library(R"FIDL(library example;

type Message = struct {
    f alias_of_vector_of_string;
};

alias alias_of_vector_of_string = vector<string>;
)FIDL");
  ASSERT_COMPILED(library);
  auto msg = library.LookupStruct("Message");
  ASSERT_NOT_NULL(msg);
  ASSERT_EQ(msg->members.size(), 1);

  auto type = GetType(msg->members[0].type_ctor);
  ASSERT_EQ(type->kind, fidl::flat::Type::Kind::kVector);
  ASSERT_EQ(type->nullability, fidl::types::Nullability::kNonnullable);

  auto vector_type = static_cast<const fidl::flat::VectorType*>(type);
  ASSERT_EQ(vector_type->element_type->kind, fidl::flat::Type::Kind::kString);
  ASSERT_EQ(static_cast<uint32_t>(*vector_type->element_count),
            static_cast<uint32_t>(fidl::flat::Size::Max()));

  auto invocation = GetLayoutInvocation(msg->members[0].type_ctor);
  ASSERT_NOT_NULL(invocation.from_type_alias);
  EXPECT_STR_EQ(fidl::NameFlatName(invocation.from_type_alias->name).c_str(),
                "example/alias_of_vector_of_string");
  EXPECT_NULL(invocation.element_type_resolved);
  EXPECT_NULL(invocation.size_resolved);
  EXPECT_EQ(invocation.nullability, fidl::types::Nullability::kNonnullable);
}

TEST(AliasTests, BadVectorParameterizedOnUse) {
  TestLibrary library(R"FIDL(
library example;

type Message = struct {
    f alias_of_vector<uint8>;
};

alias alias_of_vector = vector;
)FIDL");
  // NOTE(fxbug.dev/72924): A more general error is thrown in the new syntax
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrWrongNumberOfLayoutParameters);
}

TEST(AliasTests, BadVectorBoundedOnDecl) {
  TestLibrary library(R"FIDL(
library example;

type Message = struct {
    f alias_of_vector_max_8<string>;
};

alias alias_of_vector_max_8 = vector:8;
)FIDL");
  // NOTE(fxbug.dev/72924): A more general error is thrown in the new syntax
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrWrongNumberOfLayoutParameters);
}

TEST(AliasTests, GoodVectorBoundedOnUse) {
  TestLibrary library(R"FIDL(library example;

type Message = struct {
    f alias_of_vector_of_string:8;
};

alias alias_of_vector_of_string = vector<string>;
)FIDL");
  ASSERT_COMPILED(library);
  auto msg = library.LookupStruct("Message");
  ASSERT_NOT_NULL(msg);
  ASSERT_EQ(msg->members.size(), 1);

  auto type = GetType(msg->members[0].type_ctor);
  ASSERT_EQ(type->kind, fidl::flat::Type::Kind::kVector);
  ASSERT_EQ(type->nullability, fidl::types::Nullability::kNonnullable);

  auto vector_type = static_cast<const fidl::flat::VectorType*>(type);
  ASSERT_EQ(vector_type->element_type->kind, fidl::flat::Type::Kind::kString);
  ASSERT_EQ(static_cast<uint32_t>(*vector_type->element_count), 8u);

  auto invocation = GetLayoutInvocation(msg->members[0].type_ctor);
  ASSERT_NOT_NULL(invocation.from_type_alias);
  EXPECT_STR_EQ(fidl::NameFlatName(invocation.from_type_alias->name).c_str(),
                "example/alias_of_vector_of_string");
  EXPECT_NULL(invocation.element_type_resolved);
  EXPECT_NOT_NULL(invocation.size_resolved);
  EXPECT_EQ(static_cast<uint32_t>(*invocation.size_resolved), 8u);
  EXPECT_EQ(invocation.nullability, fidl::types::Nullability::kNonnullable);
}

TEST(AliasTests, GoodVectorNullableOnDecl) {
  TestLibrary library(R"FIDL(library example;

type Message = struct {
    f alias_of_vector_of_string_nullable;
};

alias alias_of_vector_of_string_nullable = vector<string>:optional;
)FIDL");
  ASSERT_COMPILED(library);
  auto msg = library.LookupStruct("Message");
  ASSERT_NOT_NULL(msg);
  ASSERT_EQ(msg->members.size(), 1);

  auto type = GetType(msg->members[0].type_ctor);
  ASSERT_EQ(type->kind, fidl::flat::Type::Kind::kVector);
  ASSERT_EQ(type->nullability, fidl::types::Nullability::kNullable);

  auto vector_type = static_cast<const fidl::flat::VectorType*>(type);
  ASSERT_EQ(vector_type->element_type->kind, fidl::flat::Type::Kind::kString);
  ASSERT_EQ(static_cast<uint32_t>(*vector_type->element_count),
            static_cast<uint32_t>(fidl::flat::Size::Max()));

  auto invocation = GetLayoutInvocation(msg->members[0].type_ctor);
  ASSERT_NOT_NULL(invocation.from_type_alias);
  EXPECT_STR_EQ(fidl::NameFlatName(invocation.from_type_alias->name).c_str(),
                "example/alias_of_vector_of_string_nullable");
  EXPECT_NULL(invocation.element_type_resolved);
  EXPECT_NULL(invocation.size_resolved);
  EXPECT_EQ(invocation.nullability, fidl::types::Nullability::kNonnullable);
}

TEST(AliasTests, GoodVectorNullableOnUse) {
  TestLibrary library(R"FIDL(library example;

type Message = struct {
    f alias_of_vector_of_string:optional;
};

alias alias_of_vector_of_string = vector<string>;
)FIDL");
  ASSERT_COMPILED(library);
  auto msg = library.LookupStruct("Message");
  ASSERT_NOT_NULL(msg);
  ASSERT_EQ(msg->members.size(), 1);

  auto type = GetType(msg->members[0].type_ctor);
  ASSERT_EQ(type->kind, fidl::flat::Type::Kind::kVector);
  ASSERT_EQ(type->nullability, fidl::types::Nullability::kNullable);

  auto vector_type = static_cast<const fidl::flat::VectorType*>(type);
  ASSERT_EQ(vector_type->element_type->kind, fidl::flat::Type::Kind::kString);
  ASSERT_EQ(static_cast<uint32_t>(*vector_type->element_count),
            static_cast<uint32_t>(fidl::flat::Size::Max()));

  auto invocation = GetLayoutInvocation(msg->members[0].type_ctor);
  ASSERT_NOT_NULL(invocation.from_type_alias);
  EXPECT_STR_EQ(fidl::NameFlatName(invocation.from_type_alias->name).c_str(),
                "example/alias_of_vector_of_string");
  EXPECT_NULL(invocation.element_type_resolved);
  EXPECT_NULL(invocation.size_resolved);
  EXPECT_EQ(invocation.nullability, fidl::types::Nullability::kNullable);
}

TEST(AliasTests, BadCannotParameterizeTwice) {
  TestLibrary library(R"FIDL(
library example;

type Message = struct {
    f alias_of_vector_of_string<string>;
};

alias alias_of_vector_of_string = vector<string>;
)FIDL");
  // NOTE(fxbug.dev/72924): A more general error is thrown in the new syntax
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrWrongNumberOfLayoutParameters);
}

TEST(AliasTests, BadCannotBoundTwice) {
  TestLibrary library(R"FIDL(
library example;

type Message = struct {
    f alias_of_vector_of_string_max_5:9;
};

alias alias_of_vector_of_string_max_5 = vector<string>:5;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBoundTwice);
}

TEST(AliasTests, BadCannotNullTwice) {
  TestLibrary library(R"FIDL(
library example;

type Message = struct {
    f alias_of_vector_nullable:optional;
};

alias alias_of_vector_nullable = vector<string>:optional;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotIndicateNullabilityTwice);
}

TEST(AliasTests, GoodMultiFileAliasReference) {
  TestLibrary library("first.fidl", R"FIDL(
library example;

type Protein = struct {
  amino_acids AminoAcids;
};
)FIDL");

  library.AddSource("second.fidl", R"FIDL(library example;

alias AminoAcids = vector<uint64>:32;
)FIDL");

  ASSERT_COMPILED(library);
}

TEST(AliasTests, GoodMultiFileNullableAliasReference) {
  TestLibrary library("first.fidl", R"FIDL(
library example;

type Protein = struct {
    amino_acids AminoAcids:optional;
};
)FIDL");

  library.AddSource("second.fidl", R"FIDL(library example;

alias AminoAcids = vector<uint64>:32;
)FIDL");

  ASSERT_COMPILED(library);
}

TEST(AliasTests, BadRecursiveAlias) {
  TestLibrary library("first.fidl", R"FIDL(
library example;

alias TheAlias = TheStruct;

type TheStruct = struct {
    many_mini_me vector<TheAlias>;
};
)FIDL");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrIncludeCycle);

  // TODO(fxbug.dev/35218): once recursive type handling is improved, the error message should be
  // more granular and should be asserted here.
}

TEST(AliasTests, BadCompoundIdentifier) {
  TestLibrary library("test.fidl", R"FIDL(
library example;

alias foo.bar.baz = uint8;
)FIDL");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind);
}

TEST(AliasTests, GoodUsingLibrary) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(library dependent;

type Bar = struct {
    s int8;
};
)FIDL",
                         &shared);
  ASSERT_COMPILED(dependency);

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent;

alias Bar2 = dependent.Bar;

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_COMPILED(library);
}

}  // namespace
