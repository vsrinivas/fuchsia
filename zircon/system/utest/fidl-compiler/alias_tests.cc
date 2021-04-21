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

TEST(AliasTests, BadDuplicateAliasAndUsingOld) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_int16 f;
};

alias alias_of_int16 = int16;
alias alias_of_int16 = int16;
)FIDL");
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

  auto type = msg->members[0].type_ctor->type;
  ASSERT_EQ(type->kind, fidl::flat::Type::Kind::kPrimitive);
  ASSERT_EQ(type->nullability, fidl::types::Nullability::kNonnullable);
  ASSERT_TRUE(msg->members[0].type_ctor->from_type_alias);

  auto primitive_type = static_cast<const fidl::flat::PrimitiveType*>(type);
  ASSERT_EQ(primitive_type->subtype, fidl::types::PrimitiveSubtype::kInt16);

  auto from_type_alias = msg->members[0].type_ctor->from_type_alias.value();
  EXPECT_STR_EQ(fidl::NameFlatName(from_type_alias.decl->name).c_str(), "example/alias_of_int16");
  EXPECT_NULL(from_type_alias.maybe_arg_type);
  EXPECT_NULL(from_type_alias.maybe_size);
  EXPECT_EQ(from_type_alias.nullability, fidl::types::Nullability::kNonnullable);
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

  auto type = msg->members[0].type_ctor->type;
  ASSERT_EQ(type->kind, fidl::flat::Type::Kind::kPrimitive);
  ASSERT_EQ(type->nullability, fidl::types::Nullability::kNonnullable);

  auto primitive_type = static_cast<const fidl::flat::PrimitiveType*>(type);
  ASSERT_EQ(primitive_type->subtype, fidl::types::PrimitiveSubtype::kInt16);

  auto from_type_alias = msg->members[0].type_ctor->from_type_alias.value();
  EXPECT_STR_EQ(fidl::NameFlatName(from_type_alias.decl->name).c_str(), "example/alias_of_int16");
  EXPECT_NULL(from_type_alias.maybe_arg_type);
  EXPECT_NULL(from_type_alias.maybe_size);
  EXPECT_EQ(from_type_alias.nullability, fidl::types::Nullability::kNonnullable);
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

TEST(AliasTests, BadPrimitiveTypeShadowingOld) {
  TestLibrary library(R"FIDL(
library example;

alias uint32 = uint32;

struct Message {
    uint32 f;
};
)FIDL");
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
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "int64");
}

TEST(AliasTests, BadNoOptionalOnPrimitiveOld) {
  TestLibrary library(R"FIDL(
library test.optionals;

struct Bad {
    int64? opt_num;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBeNullable);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "int64");
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
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "int64");
}

TEST(AliasTests, BadNoOptionalOnAliasedPrimitiveOld) {
  TestLibrary library(R"FIDL(
library test.optionals;

alias alias = int64;

struct Bad {
    alias? opt_num;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBeNullable);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "int64");
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

  auto type = msg->members[0].type_ctor->type;
  ASSERT_EQ(type->kind, fidl::flat::Type::Kind::kVector);
  ASSERT_EQ(type->nullability, fidl::types::Nullability::kNonnullable);

  auto vector_type = static_cast<const fidl::flat::VectorType*>(type);
  ASSERT_EQ(vector_type->element_type->kind, fidl::flat::Type::Kind::kString);
  ASSERT_EQ(static_cast<uint32_t>(*vector_type->element_count),
            static_cast<uint32_t>(fidl::flat::Size::Max()));

  auto from_type_alias = msg->members[0].type_ctor->from_type_alias.value();
  EXPECT_STR_EQ(fidl::NameFlatName(from_type_alias.decl->name).c_str(),
                "example/alias_of_vector_of_string");
  EXPECT_NULL(from_type_alias.maybe_arg_type);
  EXPECT_NULL(from_type_alias.maybe_size);
  EXPECT_EQ(from_type_alias.nullability, fidl::types::Nullability::kNonnullable);
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
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrMustBeParameterized,
                                      fidl::ErrCannotParameterizeAlias);
}

TEST(AliasTests, BadVectorParameterizedOnUseOld) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_vector<uint8> f;
};

alias alias_of_vector = vector;
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrMustBeParameterized,
                                      fidl::ErrCannotParameterizeAlias);
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
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrMustBeParameterized,
                                      fidl::ErrCannotParameterizeAlias);
}

TEST(AliasTests, BadVectorBoundedOnDeclOld) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_vector_max_8<string> f;
};

alias alias_of_vector_max_8 = vector:8;
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrMustBeParameterized,
                                      fidl::ErrCannotParameterizeAlias);
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

  auto type = msg->members[0].type_ctor->type;
  ASSERT_EQ(type->kind, fidl::flat::Type::Kind::kVector);
  ASSERT_EQ(type->nullability, fidl::types::Nullability::kNonnullable);

  auto vector_type = static_cast<const fidl::flat::VectorType*>(type);
  ASSERT_EQ(vector_type->element_type->kind, fidl::flat::Type::Kind::kString);
  ASSERT_EQ(static_cast<uint32_t>(*vector_type->element_count), 8u);

  auto from_type_alias = msg->members[0].type_ctor->from_type_alias.value();
  EXPECT_STR_EQ(fidl::NameFlatName(from_type_alias.decl->name).c_str(),
                "example/alias_of_vector_of_string");
  EXPECT_NULL(from_type_alias.maybe_arg_type);
  EXPECT_NOT_NULL(from_type_alias.maybe_size);
  EXPECT_EQ(static_cast<uint32_t>(*from_type_alias.maybe_size), 8u);
  EXPECT_EQ(from_type_alias.nullability, fidl::types::Nullability::kNonnullable);
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

  auto type = msg->members[0].type_ctor->type;
  ASSERT_EQ(type->kind, fidl::flat::Type::Kind::kVector);
  ASSERT_EQ(type->nullability, fidl::types::Nullability::kNullable);

  auto vector_type = static_cast<const fidl::flat::VectorType*>(type);
  ASSERT_EQ(vector_type->element_type->kind, fidl::flat::Type::Kind::kString);
  ASSERT_EQ(static_cast<uint32_t>(*vector_type->element_count),
            static_cast<uint32_t>(fidl::flat::Size::Max()));

  auto from_type_alias = msg->members[0].type_ctor->from_type_alias.value();
  EXPECT_STR_EQ(fidl::NameFlatName(from_type_alias.decl->name).c_str(),
                "example/alias_of_vector_of_string_nullable");
  EXPECT_NULL(from_type_alias.maybe_arg_type);
  EXPECT_NULL(from_type_alias.maybe_size);
  EXPECT_EQ(from_type_alias.nullability, fidl::types::Nullability::kNonnullable);
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

  auto type = msg->members[0].type_ctor->type;
  ASSERT_EQ(type->kind, fidl::flat::Type::Kind::kVector);
  ASSERT_EQ(type->nullability, fidl::types::Nullability::kNullable);

  auto vector_type = static_cast<const fidl::flat::VectorType*>(type);
  ASSERT_EQ(vector_type->element_type->kind, fidl::flat::Type::Kind::kString);
  ASSERT_EQ(static_cast<uint32_t>(*vector_type->element_count),
            static_cast<uint32_t>(fidl::flat::Size::Max()));

  auto from_type_alias = msg->members[0].type_ctor->from_type_alias.value();
  EXPECT_STR_EQ(fidl::NameFlatName(from_type_alias.decl->name).c_str(),
                "example/alias_of_vector_of_string");
  EXPECT_NULL(from_type_alias.maybe_arg_type);
  EXPECT_NULL(from_type_alias.maybe_size);
  EXPECT_EQ(from_type_alias.nullability, fidl::types::Nullability::kNullable);
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
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotParameterizeAlias);
}

TEST(AliasTests, BadCannotParameterizeTwiceOld) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_vector_of_string<string> f;
};

alias alias_of_vector_of_string = vector<string>;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotParameterizeAlias);
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

TEST(AliasTests, BadCannotBoundTwiceOld) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_vector_of_string_max_5:9 f;
};

alias alias_of_vector_of_string_max_5 = vector<string>:5;
)FIDL");
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

TEST(AliasTests, BadCannotNullTwiceOld) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_vector_nullable? f;
};

alias alias_of_vector_nullable = vector<string>?;
)FIDL");
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

TEST(AliasTests, BadRecursiveAliasOld) {
  TestLibrary library("first.fidl", R"FIDL(
library example;

alias TheAlias = TheStruct;

struct TheStruct {
    vector<TheAlias> many_mini_me;
};
)FIDL");

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

TEST(AliasTests, BadCompoundIdentifierOld) {
  TestLibrary library("test.fidl", R"FIDL(
library example;

alias foo.bar.baz = uint8;
)FIDL");

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

}  // namespace
