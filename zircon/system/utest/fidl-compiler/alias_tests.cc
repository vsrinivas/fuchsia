// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/names.h>
#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

TEST(AliasTests, BadDuplicateAliasAndUsing) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_int16 f;
};

alias alias_of_int16 = int16;
using alias_of_int16 = int16;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());
  ASSERT_ERR(errors[0], fidl::ErrNameCollision);
}

TEST(AliasTests, GoodPrimitive) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_int16 f;
};

alias alias_of_int16 = int16;
)FIDL");
  ASSERT_TRUE(library.Compile());
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
  ASSERT_TRUE(library.Compile());
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
  TestLibrary library(R"FIDL(
library example;

alias uint32 = uint32;

struct Message {
    uint32 f;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());
  ASSERT_ERR(errors[0], fidl::ErrIncludeCycle);
}

TEST(AliasTests, BadNoOptionalOnPrimitive) {
  TestLibrary library(R"FIDL(
library test.optionals;

struct Bad {
    int64? opt_num;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());
  ASSERT_ERR(errors[0], fidl::ErrCannotBeNullable);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "int64");
}

TEST(AliasTests, BadNoOptionalOnAliasedPrimitive) {
  TestLibrary library(R"FIDL(
library test.optionals;

alias alias = int64;

struct Bad {
    alias? opt_num;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());
  ASSERT_ERR(errors[0], fidl::ErrCannotBeNullable);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "int64");
}

TEST(AliasTests, GoodVectorParametrizedOnDecl) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_vector_of_string f;
};

alias alias_of_vector_of_string = vector<string>;
)FIDL");
  ASSERT_TRUE(library.Compile());
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

TEST(AliasTests, BadVectorParametrizedOnUse) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_vector<uint8> f;
};

alias alias_of_vector = vector;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrMustBeParameterized);
}

TEST(AliasTests, BadVectorBoundedOnDecl) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_vector_max_8<string> f;
};

alias alias_of_vector_max_8 = vector:8;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrMustBeParameterized);
}

TEST(AliasTests, GoodVectorBoundedOnUse) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_vector_of_string:8 f;
};

alias alias_of_vector_of_string = vector<string>;
)FIDL");
  ASSERT_TRUE(library.Compile());
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
  ASSERT_TRUE(library.Compile());
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
  ASSERT_TRUE(library.Compile());
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

TEST(AliasTests, BadCannotParametrizeTwice) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_vector_of_string<string> f;
};

alias alias_of_vector_of_string = vector<string>;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrCannotParametrizeTwice);
}

TEST(AliasTests, BadCannotBoundTwice) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_vector_of_string_max_5:9 f;
};

alias alias_of_vector_of_string_max_5 = vector<string>:5;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrCannotBoundTwice);
}

TEST(AliasTests, BadCannotNullTwice) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_vector_nullable<string>? f;
};

alias alias_of_vector_nullable = vector?;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrMustBeParameterized);
  ASSERT_ERR(errors[1], fidl::ErrCannotIndicateNullabilityTwice);
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

  ASSERT_TRUE(library.Compile());
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

  ASSERT_TRUE(library.Compile());
}

TEST(AliasTests, BadRecursiveAlias) {
  TestLibrary library("first.fidl", R"FIDL(
library example;

alias TheAlias = TheStruct;

struct TheStruct {
    vector<TheAlias> many_mini_me;
};
)FIDL");

  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());

  // TODO(fxbug.dev/35218): once recursive type handling is improved, the error message should be
  // more granular and should be asserted here.
}

TEST(AliasTests, BadCompoundIdentifier) {
  TestLibrary library("test.fidl", R"FIDL(
library example;

alias foo.bar.baz = uint8;
)FIDL");

  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());
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
  ASSERT_TRUE(dependency.Compile());

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent;

alias Bar2 = dependent.Bar;

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_TRUE(library.Compile());
}

TEST(AliasTests, BadDisallowOldUsingSyntax) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kDisallowOldUsingSyntax);
  TestLibrary library(R"FIDL(
library example;

using alias_of_int16 = int16;

)FIDL",
                      std::move(experimental_flags));
  ASSERT_FALSE(library.Compile());

  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());
  ASSERT_ERR(errors[0], fidl::ErrOldUsingSyntaxDeprecated);
}

}  // namespace
// TODO(pascallouis): Test various handle parametrization scenarios, and
// capture maybe_handle_subtype into FromTypeAlias struct.
// As noted in the TypeAliasTypeTemplate, there is a bug currently where
// handle parametrization of a type template is not properly passed down,
// and as a result gets lost.
