// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/names.h>
#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

TEST(TypeAliasTests, primitive) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_int16 f;
};

using alias_of_int16 = int16;
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

TEST(TypeAliasTests, primitive_type_alias_before_use) {
  TestLibrary library(R"FIDL(
library example;

using alias_of_int16 = int16;

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

TEST(TypeAliasTests, invalid_primitive_type_shadowing) {
  TestLibrary library(R"FIDL(
library example;

using uint32 = uint32;

struct Message {
    uint32 f;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());
  ASSERT_ERR(errors[0], fidl::ErrIncludeCycle);
}

TEST(TypeAliasTests, invalid_no_optional_on_primitive) {
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

TEST(TypeAliasTests, invalid_no_optional_on_aliased_primitive) {
  TestLibrary library(R"FIDL(
library test.optionals;

using alias = int64;

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

TEST(TypeAliasTests, vector_parametrized_on_decl) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_vector_of_string f;
};

using alias_of_vector_of_string = vector<string>;
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

TEST(TypeAliasTests, vector_parametrized_on_use) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_vector<uint8> f;
};

using alias_of_vector = vector;
)FIDL");
  ASSERT_TRUE(library.Compile());
  auto msg = library.LookupStruct("Message");
  ASSERT_NOT_NULL(msg);
  ASSERT_EQ(msg->members.size(), 1);

  auto type = msg->members[0].type_ctor->type;
  ASSERT_EQ(type->kind, fidl::flat::Type::Kind::kVector);
  ASSERT_EQ(type->nullability, fidl::types::Nullability::kNonnullable);

  auto vector_type = static_cast<const fidl::flat::VectorType*>(type);
  ASSERT_EQ(vector_type->element_type->kind, fidl::flat::Type::Kind::kPrimitive);
  ASSERT_EQ(static_cast<uint32_t>(*vector_type->element_count),
            static_cast<uint32_t>(fidl::flat::Size::Max()));

  auto primitive_element_type =
      static_cast<const fidl::flat::PrimitiveType*>(vector_type->element_type);
  ASSERT_EQ(primitive_element_type->subtype, fidl::types::PrimitiveSubtype::kUint8);

  auto from_type_alias = msg->members[0].type_ctor->from_type_alias.value();
  EXPECT_STR_EQ(fidl::NameFlatName(from_type_alias.decl->name).c_str(), "example/alias_of_vector");
  EXPECT_NOT_NULL(from_type_alias.maybe_arg_type);
  auto from_type_alias_arg_type = from_type_alias.maybe_arg_type;
  ASSERT_EQ(from_type_alias_arg_type->kind, fidl::flat::Type::Kind::kPrimitive);
  auto from_type_alias_arg_primitive_type =
      static_cast<const fidl::flat::PrimitiveType*>(from_type_alias_arg_type);
  ASSERT_EQ(from_type_alias_arg_primitive_type->subtype, fidl::types::PrimitiveSubtype::kUint8);
  EXPECT_NULL(from_type_alias.maybe_size);
  EXPECT_EQ(from_type_alias.nullability, fidl::types::Nullability::kNonnullable);
}

TEST(TypeAliasTests, vector_bounded_on_decl) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_vector_max_8<string> f;
};

using alias_of_vector_max_8 = vector:8;
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
                "example/alias_of_vector_max_8");
  EXPECT_NOT_NULL(from_type_alias.maybe_arg_type);
  auto from_type_alias_arg_type = from_type_alias.maybe_arg_type;
  ASSERT_EQ(from_type_alias_arg_type->kind, fidl::flat::Type::Kind::kString);
  EXPECT_NULL(from_type_alias.maybe_size);
  EXPECT_EQ(from_type_alias.nullability, fidl::types::Nullability::kNonnullable);
}

TEST(TypeAliasTests, vector_bounded_on_use) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_vector_of_string:8 f;
};

using alias_of_vector_of_string = vector<string>;
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

TEST(TypeAliasTests, vector_nullable_on_decl) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_vector_of_string_nullable f;
};

using alias_of_vector_of_string_nullable = vector<string>?;
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

TEST(TypeAliasTests, vector_nullable_on_use) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_vector_of_string? f;
};

using alias_of_vector_of_string = vector<string>;
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

TEST(TypeAliasTests, invalid_cannot_parametrize_twice) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_vector_of_string<string> f;
};

using alias_of_vector_of_string = vector<string>;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrCannotParametrizeTwice);
}

TEST(TypeAliasTests, invalid_cannot_bound_twice) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_vector_of_string_max_5:9 f;
};

using alias_of_vector_of_string_max_5 = vector<string>:5;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrCannotBoundTwice);
}

TEST(TypeAliasTests, invalid_cannot_null_twice) {
  TestLibrary library(R"FIDL(
library example;

struct Message {
    alias_of_vector_nullable<string>? f;
};

using alias_of_vector_nullable = vector?;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrCannotIndicateNullabilityTwice);
}

TEST(TypeAliasTests, multi_file_alias_reference) {
  TestLibrary library("first.fidl", R"FIDL(
library example;

struct Protein {
    AminoAcids amino_acids;
};
)FIDL");

  library.AddSource("second.fidl", R"FIDL(
library example;

using AminoAcids = vector<uint64>:32;
)FIDL");

  ASSERT_TRUE(library.Compile());
}

TEST(TypeAliasTests, multi_file_nullable_alias_reference) {
  TestLibrary library("first.fidl", R"FIDL(
library example;

struct Protein {
    AminoAcids? amino_acids;
};
)FIDL");

  library.AddSource("second.fidl", R"FIDL(
library example;

using AminoAcids = vector<uint64>:32;
)FIDL");

  ASSERT_TRUE(library.Compile());
}

TEST(TypeAliasTests, invalid_recursive_alias) {
  TestLibrary library("first.fidl", R"FIDL(
library example;

using TheAlias = TheStruct;

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

TEST(TypeAliasTests, invalid_compound_identifier) {
  TestLibrary library("test.fidl", R"FIDL(
library example;

using foo.bar.baz = uint8;
)FIDL");

  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());
  ASSERT_ERR(errors[0], fidl::ErrCompoundAliasIdentifier);
}

}  // namespace
// TODO(pascallouis): Test various handle parametrization scenarios, and
// capture maybe_handle_subtype into FromTypeAlias struct.
// As noted in the TypeAliasTypeTemplate, there is a bug currently where
// handle parametrization of a type template is not properly passed down,
// and as a result gets lost.
