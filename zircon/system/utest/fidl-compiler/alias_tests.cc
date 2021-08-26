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
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Message = struct {
    f alias_of_int16;
};

alias alias_of_int16 = int16;
alias alias_of_int16 = int16;
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameCollision);
}

TEST(AliasTests, GoodPrimitive) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_int16 f;
};

alias alias_of_int16 = int16;
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
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
  TestLibrary library(R"FIDL(
library example;

alias alias_of_int16 = int16;

struct Message {
    alias_of_int16 f;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
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
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

alias uint32 = uint32;

type Message = struct {
    f uint32;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrIncludeCycle);
}

TEST(AliasTests, BadNoOptionalOnPrimitive) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library test.optionals;

type Bad = struct {
    opt_num int64:optional;
};

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBeNullable);
}

TEST(AliasTests, BadMultipleConstraintsOnPrimitive) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library test.optionals;

type Bad = struct {
    opt_num int64:<optional, foo, bar>;
};

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTooManyConstraints);
}

TEST(AliasTests, BadNoOptionalOnAliasedPrimitive) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library test.optionals;

alias alias = int64;

type Bad = struct {
    opt_num alias:optional;
};

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBeNullable);
}

TEST(AliasTests, GoodVectorParameterizedOnDecl) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_vector_of_string f;
};

alias alias_of_vector_of_string = vector<string>;
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
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
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Message = struct {
    f alias_of_vector<uint8>;
};

alias alias_of_vector = vector;
)FIDL",
                      experimental_flags);
  // NOTE(fxbug.dev/72924): A more general error is thrown in the new syntax
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrWrongNumberOfLayoutParameters);
}

TEST(AliasTests, BadVectorBoundedOnDecl) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Message = struct {
    f alias_of_vector_max_8<string>;
};

alias alias_of_vector_max_8 = vector:8;
)FIDL",
                      experimental_flags);
  // NOTE(fxbug.dev/72924): A more general error is thrown in the new syntax
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrWrongNumberOfLayoutParameters);
}

TEST(AliasTests, GoodVectorBoundedOnUse) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_vector_of_string:8 f;
};

alias alias_of_vector_of_string = vector<string>;
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
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
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_vector_of_string_nullable f;
};

alias alias_of_vector_of_string_nullable = vector<string>?;
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
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
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_vector_of_string? f;
};

alias alias_of_vector_of_string = vector<string>;
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
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
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Message = struct {
    f alias_of_vector_of_string<string>;
};

alias alias_of_vector_of_string = vector<string>;
)FIDL",
                      experimental_flags);
  // NOTE(fxbug.dev/72924): A more general error is thrown in the new syntax
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrWrongNumberOfLayoutParameters);
}

TEST(AliasTests, BadCannotBoundTwice) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Message = struct {
    f alias_of_vector_of_string_max_5:9;
};

alias alias_of_vector_of_string_max_5 = vector<string>:5;
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBoundTwice);
}

TEST(AliasTests, BadCannotNullTwice) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Message = struct {
    f alias_of_vector_nullable:optional;
};

alias alias_of_vector_nullable = vector<string>:optional;
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotIndicateNullabilityTwice);
}

TEST(AliasTests, GoodMultiFileAliasReference) {
  TestLibrary library("first.fidl", R"FIDL(
library example;

struct Protein {
    AminoAcids amino_acids;
};
)FIDL");

  library.AddSource("second.fidl", R"FIDL(
library example;

alias AminoAcids = vector<uint64>:32;
)FIDL");

  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(AliasTests, GoodMultiFileNullableAliasReference) {
  TestLibrary library("first.fidl", R"FIDL(
library example;

struct Protein {
    AminoAcids? amino_acids;
};
)FIDL");

  library.AddSource("second.fidl", R"FIDL(
library example;

alias AminoAcids = vector<uint64>:32;
)FIDL");

  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(AliasTests, BadRecursiveAlias) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library("first.fidl", R"FIDL(
library example;

alias TheAlias = TheStruct;

type TheStruct = struct {
    many_mini_me vector<TheAlias>;
};
)FIDL",
                      experimental_flags);

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrIncludeCycle);

  // TODO(fxbug.dev/35218): once recursive type handling is improved, the error message should be
  // more granular and should be asserted here.
}

TEST(AliasTests, BadCompoundIdentifier) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library("test.fidl", R"FIDL(
library example;

alias foo.bar.baz = uint8;
)FIDL",
                      experimental_flags);

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind);
}

TEST(AliasTests, GoodUsingLibrary) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

struct Bar {
  int8 s;
};

)FIDL",
                         &shared);
  TestLibrary converted_dependency;
  ASSERT_COMPILED_AND_CONVERT_INTO(dependency, converted_dependency);

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent;

alias Bar2 = dependent.Bar;

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_COMPILED_AND_CONVERT_WITH_DEP(library, converted_dependency);
}

TEST(AliasTests, GoodUsingLibraryWithOldDep) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

struct Bar {
  int8 s;
};

)FIDL",
                         &shared);
  TestLibrary cloned_dependency;
  ASSERT_COMPILED_AND_CLONE_INTO(dependency, cloned_dependency);

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent;

alias Bar2 = dependent.Bar;

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_COMPILED_AND_CONVERT_WITH_DEP(library, cloned_dependency);
}

// This test documents the faulty behavior of handle aliases in the old syntax:
// since the alias isn't named "handle", we try to parse a subtype/size rather
// than handle constraints.
TEST(AliasTests, BadHandleAlias) {
  auto library = WithLibraryZx(R"FIDL(
library example;

using zx;

alias my_handle = zx.handle:VMO;

resource struct MyStruct {
    my_handle:3 h;
};
)FIDL",
                               fidl::ExperimentalFlags());

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotHaveSize)
}

TEST(AliasTests, BadBoundsOnRequestType) {
  // Test that CreateInvocation::Size doesn't get ignored on type aliases when
  // applying constraints
  TestLibrary library(R"FIDL(
library example;

protocol Foo {};

alias MyRequest = request<Foo>;

struct Data {
  MyRequest:10 foo;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotHaveSize);
}

TEST(AliasTests, BadBoundsOnArray) {
  // Test that CreateInvocation::Size doesn't get ignored on type aliases when
  // applying constraints
  TestLibrary library(R"FIDL(
library example;

alias MyArray = array<uint8>:10;

struct Data {
  MyArray:10 foo;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotParameterizeAlias);
}

}  // namespace
