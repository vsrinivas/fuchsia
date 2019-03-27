// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include "test_library.h"

namespace {

bool primitive() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

using alias_of_int16 = int16;

struct Message {
    alias_of_int16 f;
};
)FIDL");
    ASSERT_TRUE(library.Compile());
    auto msg = library.LookupStruct("Message");
    ASSERT_NONNULL(msg);
    ASSERT_EQ(msg->members.size(), 1);

    auto type = msg->members[0].type_ctor->type;
    ASSERT_EQ(type->kind, fidl::flat::Type::Kind::kPrimitive);
    ASSERT_EQ(type->nullability, fidl::types::Nullability::kNonnullable);

    auto primitive_type = static_cast<const fidl::flat::PrimitiveType*>(type);
    ASSERT_EQ(primitive_type->subtype, fidl::types::PrimitiveSubtype::kInt16);

    END_TEST;
}

bool vector_parametrized_on_decl() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

using alias_of_vector_of_string = vector<string>;

struct Message {
    alias_of_vector_of_string f;
};
)FIDL");
    ASSERT_TRUE(library.Compile());
    auto msg = library.LookupStruct("Message");
    ASSERT_NONNULL(msg);
    ASSERT_EQ(msg->members.size(), 1);

    auto type = msg->members[0].type_ctor->type;
    ASSERT_EQ(type->kind, fidl::flat::Type::Kind::kVector);
    ASSERT_EQ(type->nullability, fidl::types::Nullability::kNonnullable);

    auto vector_type = static_cast<const fidl::flat::VectorType*>(type);
    ASSERT_EQ(vector_type->element_type->kind, fidl::flat::Type::Kind::kString);
    ASSERT_EQ(static_cast<uint32_t>(*vector_type->element_count),
              static_cast<uint32_t>(fidl::flat::Size::Max()));

    END_TEST;
}

bool vector_parametrized_on_use() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

using alias_of_vector = vector;

struct Message {
    alias_of_vector<uint8> f;
};
)FIDL");
    ASSERT_TRUE(library.Compile());
    auto msg = library.LookupStruct("Message");
    ASSERT_NONNULL(msg);
    ASSERT_EQ(msg->members.size(), 1);

    auto type = msg->members[0].type_ctor->type;
    ASSERT_EQ(type->kind, fidl::flat::Type::Kind::kVector);
    ASSERT_EQ(type->nullability, fidl::types::Nullability::kNonnullable);

    auto vector_type = static_cast<const fidl::flat::VectorType*>(type);
    ASSERT_EQ(vector_type->element_type->kind, fidl::flat::Type::Kind::kPrimitive);
    ASSERT_EQ(static_cast<uint32_t>(*vector_type->element_count),
              static_cast<uint32_t>(fidl::flat::Size::Max()));

    auto primitive_element_type = static_cast<const fidl::flat::PrimitiveType*>(vector_type->element_type);
    ASSERT_EQ(primitive_element_type->subtype, fidl::types::PrimitiveSubtype::kUint8);

    END_TEST;
}

bool vector_bounded_on_decl() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

using alias_of_vector_max_8 = vector:8;

struct Message {
    alias_of_vector_max_8<string> f;
};
)FIDL");
    ASSERT_TRUE(library.Compile());
    auto msg = library.LookupStruct("Message");
    ASSERT_NONNULL(msg);
    ASSERT_EQ(msg->members.size(), 1);

    auto type = msg->members[0].type_ctor->type;
    ASSERT_EQ(type->kind, fidl::flat::Type::Kind::kVector);
    ASSERT_EQ(type->nullability, fidl::types::Nullability::kNonnullable);

    auto vector_type = static_cast<const fidl::flat::VectorType*>(type);
    ASSERT_EQ(vector_type->element_type->kind, fidl::flat::Type::Kind::kString);
    ASSERT_EQ(static_cast<uint32_t>(*vector_type->element_count),
              8u);

    END_TEST;
}

bool vector_bounded_on_use() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

using alias_of_vector_of_string = vector<string>;

struct Message {
    alias_of_vector_of_string:8 f;
};
)FIDL");
    ASSERT_TRUE(library.Compile());
    auto msg = library.LookupStruct("Message");
    ASSERT_NONNULL(msg);
    ASSERT_EQ(msg->members.size(), 1);

    auto type = msg->members[0].type_ctor->type;
    ASSERT_EQ(type->kind, fidl::flat::Type::Kind::kVector);
    ASSERT_EQ(type->nullability, fidl::types::Nullability::kNonnullable);

    auto vector_type = static_cast<const fidl::flat::VectorType*>(type);
    ASSERT_EQ(vector_type->element_type->kind, fidl::flat::Type::Kind::kString);
    ASSERT_EQ(static_cast<uint32_t>(*vector_type->element_count),
              8u);

    END_TEST;
}

bool vector_nullable_on_decl() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

using alias_of_vector_of_string_nullable = vector<string>?;

struct Message {
    alias_of_vector_of_string_nullable f;
};
)FIDL");
    ASSERT_TRUE(library.Compile());
    auto msg = library.LookupStruct("Message");
    ASSERT_NONNULL(msg);
    ASSERT_EQ(msg->members.size(), 1);

    auto type = msg->members[0].type_ctor->type;
    ASSERT_EQ(type->kind, fidl::flat::Type::Kind::kVector);
    ASSERT_EQ(type->nullability, fidl::types::Nullability::kNullable);

    auto vector_type = static_cast<const fidl::flat::VectorType*>(type);
    ASSERT_EQ(vector_type->element_type->kind, fidl::flat::Type::Kind::kString);
    ASSERT_EQ(static_cast<uint32_t>(*vector_type->element_count),
              static_cast<uint32_t>(fidl::flat::Size::Max()));

    END_TEST;
}

bool vector_nullable_on_use() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

using alias_of_vector_of_string = vector<string>;

struct Message {
    alias_of_vector_of_string? f;
};
)FIDL");
    ASSERT_TRUE(library.Compile());
    auto msg = library.LookupStruct("Message");
    ASSERT_NONNULL(msg);
    ASSERT_EQ(msg->members.size(), 1);

    auto type = msg->members[0].type_ctor->type;
    ASSERT_EQ(type->kind, fidl::flat::Type::Kind::kVector);
    ASSERT_EQ(type->nullability, fidl::types::Nullability::kNullable);

    auto vector_type = static_cast<const fidl::flat::VectorType*>(type);
    ASSERT_EQ(vector_type->element_type->kind, fidl::flat::Type::Kind::kString);
    ASSERT_EQ(static_cast<uint32_t>(*vector_type->element_count),
              static_cast<uint32_t>(fidl::flat::Size::Max()));

    END_TEST;
}

bool invalid_cannot_parametrize_twice() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

using alias_of_vector_of_string = vector<string>;

struct Message {
    alias_of_vector_of_string<string> f;
};
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "cannot parametrize twice");

    END_TEST;
}

bool invalid_cannot_bound_twice() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

using alias_of_vector_of_string_max_5 = vector<string>:5;

struct Message {
    alias_of_vector_of_string_max_5:9 f;
};
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "cannot bound twice");

    END_TEST;
}

bool invalid_cannot_null_twice() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

using alias_of_vector_nullable = vector?;

struct Message {
    alias_of_vector_nullable<string>? f;
};
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "cannot indicate nullability twice");

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(type_alias_tests);
RUN_TEST(primitive);
RUN_TEST(vector_parametrized_on_decl);
RUN_TEST(vector_parametrized_on_use);
RUN_TEST(vector_bounded_on_decl);
RUN_TEST(vector_bounded_on_use);
RUN_TEST(vector_nullable_on_decl);
RUN_TEST(vector_nullable_on_use);
RUN_TEST(invalid_cannot_parametrize_twice);
RUN_TEST(invalid_cannot_bound_twice);
RUN_TEST(invalid_cannot_null_twice);
END_TEST_CASE(type_alias_tests);
