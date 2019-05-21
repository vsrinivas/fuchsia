// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>

#include "test_library.h"

namespace {

struct Expected {
    uint32_t size = 0;
    uint32_t alignment = 0;
    uint32_t max_out_of_line = 0;
    uint32_t max_handles = 0;
    uint32_t depth = 0;
};

bool CheckTypeShape(const TypeShape& actual, Expected expected) {
    BEGIN_HELPER;
    EXPECT_EQ(actual.Size(), expected.size);
    EXPECT_EQ(actual.Alignment(), expected.alignment);
    EXPECT_EQ(actual.MaxOutOfLine(), expected.max_out_of_line);
    EXPECT_EQ(actual.MaxHandles(), expected.max_handles);
    EXPECT_EQ(actual.Depth(), expected.depth);
    END_HELPER;
}

static bool simple_structs() {
    BEGIN_TEST;

    TestLibrary test_library(R"FIDL(
library example;

struct OneBool {
  bool b;
};

struct TwoBools {
  bool a;
  bool b;
};

struct BoolAndU32 {
  bool b;
  uint32 u;
};

struct BoolAndU64 {
  bool b;
  uint64 u;
};
    )FIDL");
    ASSERT_TRUE(test_library.Compile());

    auto one_bool = test_library.LookupStruct("OneBool");
    ASSERT_NONNULL(one_bool);
    EXPECT_TRUE(CheckTypeShape(one_bool->typeshape, Expected {
        .size = 1,
        .alignment = 1,
    }));

    auto two_bools = test_library.LookupStruct("TwoBools");
    ASSERT_NONNULL(two_bools);
    EXPECT_TRUE(CheckTypeShape(two_bools->typeshape, Expected {
        .size = 2,
        .alignment = 1,
    }));

    auto bool_and_u32 = test_library.LookupStruct("BoolAndU32");
    ASSERT_NONNULL(bool_and_u32);
    EXPECT_TRUE(CheckTypeShape(bool_and_u32->typeshape, Expected {
        .size = 8,
        .alignment = 4,
    }));

    auto bool_and_u64 = test_library.LookupStruct("BoolAndU64");
    ASSERT_NONNULL(bool_and_u64);
    EXPECT_TRUE(CheckTypeShape(bool_and_u64->typeshape, Expected {
        .size = 16,
        .alignment = 8,
    }));

    END_TEST;
}

static bool simple_structs_with_handles() {
    BEGIN_TEST;

    TestLibrary test_library(R"FIDL(
library example;

struct OneHandle {
  handle h;
};

struct TwoHandles {
  handle<channel> h1;
  handle<port> h2;
};

struct ThreeHandlesOneOptional {
  handle<channel> h1;
  handle<port> h2;
  handle<timer>? opt_h3;
};

    )FIDL");
    ASSERT_TRUE(test_library.Compile());

    auto one_handle = test_library.LookupStruct("OneHandle");
    ASSERT_NONNULL(one_handle);
    EXPECT_TRUE(CheckTypeShape(one_handle->typeshape, Expected {
        .size = 4,
        .alignment = 4,
        .max_handles = 1,
    }));

    auto two_handles = test_library.LookupStruct("TwoHandles");
    ASSERT_NONNULL(two_handles);
    EXPECT_TRUE(CheckTypeShape(two_handles->typeshape, Expected {
        .size = 8,
        .alignment = 4,
        .max_handles = 2,
    }));

    auto three_handles_one_optional = test_library.LookupStruct("ThreeHandlesOneOptional");
    ASSERT_NONNULL(three_handles_one_optional);
    EXPECT_TRUE(CheckTypeShape(three_handles_one_optional->typeshape, Expected {
        .size = 12,
        .alignment = 4,
        .max_handles = 3,
    }));

    END_TEST;
}

static bool simple_tables() {
    BEGIN_TEST;

    TestLibrary test_library(R"FIDL(
library example;

table TableWithNoMembers {
};

table TableWithOneBool {
  1: bool b;
};

table TableWithTwoBools {
  1: bool a;
  2: bool b;
};

table TableWithBoolAndU32 {
  1: bool b;
  2: uint32 u;
};

table TableWithBoolAndU64 {
  1: bool b;
  2: uint64 u;
};

    )FIDL");
    ASSERT_TRUE(test_library.Compile());

    auto no_members = test_library.LookupTable("TableWithNoMembers");
    ASSERT_NONNULL(no_members);
    EXPECT_TRUE(CheckTypeShape(no_members->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .depth = 4294967295, // TODO(FIDL-457): wrong.
    }));

    auto one_bool = test_library.LookupTable("TableWithOneBool");
    ASSERT_NONNULL(one_bool);
    EXPECT_TRUE(CheckTypeShape(one_bool->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 24,
        .depth = 3, // TODO(FIDL-457): wrong.
    }));

    auto two_bools = test_library.LookupTable("TableWithTwoBools");
    ASSERT_NONNULL(two_bools);
    EXPECT_TRUE(CheckTypeShape(two_bools->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 48,
        .depth = 3, // TODO(FIDL-457): wrong.
    }));

    auto bool_and_u32 = test_library.LookupTable("TableWithBoolAndU32");
    ASSERT_NONNULL(bool_and_u32);
    EXPECT_TRUE(CheckTypeShape(bool_and_u32->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 48,
        .depth = 3, // TODO(FIDL-457): wrong.
    }));

    auto bool_and_u64 = test_library.LookupTable("TableWithBoolAndU64");
    ASSERT_NONNULL(bool_and_u64);
    EXPECT_TRUE(CheckTypeShape(bool_and_u32->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 48,
        .depth = 3, // TODO(FIDL-457): wrong.
    }));

    END_TEST;
}

static bool simple_tables_with_handles() {
    BEGIN_TEST;

    TestLibrary test_library(R"FIDL(
library example;

table TableWithOneHandle {
  1: handle h;
};

    )FIDL");
    ASSERT_TRUE(test_library.Compile());

    auto one_handle = test_library.LookupTable("TableWithOneHandle");
    ASSERT_NONNULL(one_handle);
    EXPECT_TRUE(CheckTypeShape(one_handle->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 24,
        .max_handles = 1,
        .depth = 3,
    }));

    END_TEST;
}

static bool optional_structs() {
    BEGIN_TEST;

    TestLibrary test_library(R"FIDL(
library example;

struct OneBool {
  bool b;
};

struct OptionalOneBool {
  OneBool? s;
};

struct TwoBools {
  bool a;
  bool b;
};

struct OptionalTwoBools {
  TwoBools? s;
};

struct BoolAndU32 {
  bool b;
  uint32 u;
};

struct OptionalBoolAndU32 {
  BoolAndU32? s;
};

struct BoolAndU64 {
  bool b;
  uint64 u;
};

struct OptionalBoolAndU64 {
  BoolAndU64? s;
};

    )FIDL");
    ASSERT_TRUE(test_library.Compile());

    auto one_bool = test_library.LookupStruct("OptionalOneBool");
    ASSERT_NONNULL(one_bool);
    EXPECT_TRUE(CheckTypeShape(one_bool->typeshape, Expected {
        .size = 8,
        .alignment = 8,
        .max_out_of_line = 8,
        .depth = 1,
    }));

    auto two_bools = test_library.LookupStruct("OptionalTwoBools");
    ASSERT_NONNULL(two_bools);
    EXPECT_TRUE(CheckTypeShape(two_bools->typeshape, Expected {
        .size = 8,
        .alignment = 8,
        .max_out_of_line = 8,
        .depth = 1,
    }));

    auto bool_and_u32 = test_library.LookupStruct("OptionalBoolAndU32");
    ASSERT_NONNULL(bool_and_u32);
    EXPECT_TRUE(CheckTypeShape(bool_and_u32->typeshape, Expected {
        .size = 8,
        .alignment = 8,
        .max_out_of_line = 8,
        .depth = 1,
    }));

    auto bool_and_u64 = test_library.LookupStruct("OptionalBoolAndU64");
    ASSERT_NONNULL(bool_and_u64);
    EXPECT_TRUE(CheckTypeShape(bool_and_u64->typeshape, Expected {
        .size = 8,
        .alignment = 8,
        .max_out_of_line = 16,
        .depth = 1,
    }));

    END_TEST;
}

static bool optional_tables() {
    BEGIN_TEST;

    TestLibrary test_library(R"FIDL(
library example;

struct OneBool {
  bool b;
};

table TableWithOptionalOneBool {
  1: OneBool s;
};

table TableWithOneBool {
  1: bool b;
};

table TableWithOptionalTableWithOneBool {
  1: TableWithOneBool s;
};

struct TwoBools {
  bool a;
  bool b;
};

table TableWithOptionalTwoBools {
  1: TwoBools s;
};

table TableWithTwoBools {
  1: bool a;
  2: bool b;
};

table TableWithOptionalTableWithTwoBools {
  1: TableWithTwoBools s;
};

struct BoolAndU32 {
  bool b;
  uint32 u;
};

table TableWithOptionalBoolAndU32 {
  1: BoolAndU32 s;
};

table TableWithBoolAndU32 {
  1: bool b;
  2: uint32 u;
};

table TableWithOptionalTableWithBoolAndU32 {
  1: TableWithBoolAndU32 s;
};

struct BoolAndU64 {
  bool b;
  uint64 u;
};

table TableWithOptionalBoolAndU64 {
  1: BoolAndU64 s;
};

table TableWithBoolAndU64 {
  1: bool b;
  2: uint64 u;
};

table TableWithOptionalTableWithBoolAndU64 {
  1: TableWithBoolAndU64 s;
};

    )FIDL");
    ASSERT_TRUE(test_library.Compile());

    auto one_bool = test_library.LookupTable("TableWithOptionalOneBool");
    ASSERT_NONNULL(one_bool);
    EXPECT_TRUE(CheckTypeShape(one_bool->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 24,
        .depth = 3, // TODO(FIDL-457): wrong.
    }));

    auto table_with_one_bool = test_library.LookupTable("TableWithOptionalTableWithOneBool");
    ASSERT_NONNULL(table_with_one_bool);
    EXPECT_TRUE(CheckTypeShape(table_with_one_bool->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 56,
        .depth = 6, // TODO(FIDL-457): wrong.
    }));

    auto two_bools = test_library.LookupTable("TableWithOptionalTwoBools");
    ASSERT_NONNULL(two_bools);
    EXPECT_TRUE(CheckTypeShape(two_bools->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 24,
        .depth = 3, // TODO(FIDL-457): wrong.
    }));

    auto table_with_two_bools = test_library.LookupTable("TableWithOptionalTableWithTwoBools");
    ASSERT_NONNULL(table_with_two_bools);
    EXPECT_TRUE(CheckTypeShape(table_with_two_bools->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 80,
        .depth = 6, // TODO(FIDL-457): wrong.
    }));

    auto bool_and_u32 = test_library.LookupTable("TableWithOptionalBoolAndU32");
    ASSERT_NONNULL(bool_and_u32);
    EXPECT_TRUE(CheckTypeShape(bool_and_u32->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 24,
        .depth = 3, // TODO(FIDL-457): wrong.
    }));

    auto table_with_bool_and_u32 = test_library.LookupTable("TableWithOptionalTableWithBoolAndU32");
    ASSERT_NONNULL(table_with_bool_and_u32);
    EXPECT_TRUE(CheckTypeShape(table_with_bool_and_u32->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 80,
        .depth = 6, // TODO(FIDL-457): wrong.
    }));

    auto bool_and_u64 = test_library.LookupTable("TableWithOptionalBoolAndU64");
    ASSERT_NONNULL(bool_and_u64);
    EXPECT_TRUE(CheckTypeShape(bool_and_u64->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 32,
        .depth = 3, // TODO(FIDL-457): wrong.
    }));

    auto table_with_bool_and_u64 = test_library.LookupTable("TableWithOptionalTableWithBoolAndU64");
    ASSERT_NONNULL(table_with_bool_and_u64);
    EXPECT_TRUE(CheckTypeShape(table_with_bool_and_u64->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 80,
        .depth = 6, // TODO(FIDL-457): wrong.
    }));

    END_TEST;
}

static bool unions() {
    BEGIN_TEST;

    TestLibrary test_library(R"FIDL(
library example;

struct BoolAndU64 {
  bool b;
  uint64 u;
};

union UnionOfThings {
  bool ob;
  BoolAndU64 bu;
};

struct OptionalUnion {
  UnionOfThings? u;
};

table TableWithOptionalUnion {
  1: UnionOfThings u;
};

    )FIDL");
    ASSERT_TRUE(test_library.Compile());

    auto a_union = test_library.LookupUnion("UnionOfThings");
    ASSERT_NONNULL(a_union);
    EXPECT_TRUE(CheckTypeShape(a_union->typeshape, Expected {
        .size = 24,
        .alignment = 8,
    }));

    auto optional_union = test_library.LookupStruct("OptionalUnion");
    ASSERT_NONNULL(optional_union);
    EXPECT_TRUE(CheckTypeShape(optional_union->typeshape, Expected {
        .size = 8,
        .alignment = 8,
        .max_out_of_line = 24,
        .depth = 1,
    }));

    auto table_with_optional_union = test_library.LookupTable("TableWithOptionalUnion");
    ASSERT_NONNULL(table_with_optional_union);
    EXPECT_TRUE(CheckTypeShape(table_with_optional_union->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 40,
        .depth = 3,
    }));

    END_TEST;
}

static bool unions_with_handles() {
    BEGIN_TEST;

    TestLibrary test_library(R"FIDL(
library example;

union OneHandleUnion {
  handle one_handle;
  bool one_bool;
  uint32 one_int;
};

union ManyHandleUnion {
  handle one_handle;
  array<handle>:8 handle_array;
  vector<handle>:8 handle_vector;
};

    )FIDL");
    ASSERT_TRUE(test_library.Compile());

    auto one_handle_union = test_library.LookupUnion("OneHandleUnion");
    ASSERT_NONNULL(one_handle_union);
    EXPECT_TRUE(CheckTypeShape(one_handle_union->typeshape, Expected {
        .size = 8,
        .alignment = 4,
        .max_handles = 1,
    }));

    auto many_handle_union = test_library.LookupUnion("ManyHandleUnion");
    ASSERT_NONNULL(many_handle_union);
    EXPECT_TRUE(CheckTypeShape(many_handle_union->typeshape, Expected {
        .size = 40,
        .alignment = 8,
        .max_out_of_line = 32,
        .max_handles = 8,
        .depth = 1,
    }));

    END_TEST;
}

static bool vectors() {
    BEGIN_TEST;

    TestLibrary test_library(R"FIDL(
library example;

struct PaddedVector {
  vector<int32>:3 pv;
};

struct UnboundedVector {
  vector<int32> uv;
};

struct UnboundedVectors {
  vector<int32> uv1;
  vector<int32> uv2;
};

table TableWithPaddedVector {
  1: vector<int32>:3 pv;
};

table TableWithUnboundedVector {
  1: vector<int32> uv;
};

table TableWithUnboundedVectors {
  1: vector<int32> uv1;
  2: vector<int32> uv2;
};

    )FIDL");
    ASSERT_TRUE(test_library.Compile());

    auto padded_vector = test_library.LookupStruct("PaddedVector");
    ASSERT_NONNULL(padded_vector);
    EXPECT_TRUE(CheckTypeShape(padded_vector->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 16,
        .depth = 1,
    }));

    auto unbounded_vector = test_library.LookupStruct("UnboundedVector");
    ASSERT_NONNULL(unbounded_vector);
    EXPECT_TRUE(CheckTypeShape(unbounded_vector->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = std::numeric_limits<uint32_t>::max(),
        .depth = 1,
    }));

    auto unbounded_vectors = test_library.LookupStruct("UnboundedVectors");
    ASSERT_NONNULL(unbounded_vectors);
    EXPECT_TRUE(CheckTypeShape(unbounded_vectors->typeshape, Expected {
        .size = 32,
        .alignment = 8,
        .max_out_of_line = std::numeric_limits<uint32_t>::max(),
        .depth = 1,
    }));

    auto table_with_padded_vector = test_library.LookupTable("TableWithPaddedVector");
    ASSERT_NONNULL(table_with_padded_vector);
    EXPECT_TRUE(CheckTypeShape(table_with_padded_vector->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 48,
        .depth = 4, // TODO(FIDL-457): wrong.
    }));

    auto table_with_unbounded_vector = test_library.LookupTable("TableWithUnboundedVector");
    ASSERT_NONNULL(table_with_unbounded_vector);
    EXPECT_TRUE(CheckTypeShape(table_with_unbounded_vector->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = std::numeric_limits<uint32_t>::max(),
        .depth = 4, // TODO(FIDL-457): wrong.
    }));

    auto table_with_unbounded_vectors = test_library.LookupTable("TableWithUnboundedVectors");
    ASSERT_NONNULL(table_with_unbounded_vectors);
    EXPECT_TRUE(CheckTypeShape(table_with_unbounded_vectors->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = std::numeric_limits<uint32_t>::max(),
        .depth = 4, // TODO(FIDL-457): wrong.
    }));

    END_TEST;
}

static bool vectors_with_handles() {
    BEGIN_TEST;

    TestLibrary test_library(R"FIDL(
library example;

struct HandleVector {
  vector<handle>:8 hv;
};

struct HandleNullableVector {
  vector<handle>:8? hv;
};

table TableWithHandleVector {
  1: vector<handle>:8 hv;
};

struct UnboundedHandleVector {
  vector<handle> hv;
};

table TableWithUnboundedHandleVector {
  1: vector<handle> hv;
};

struct OneHandle {
  handle h;
};

struct HandleStructVector {
  vector<OneHandle>:8 sv;
};

table TableWithOneHandle {
  1: handle h;
};

struct HandleTableVector {
  vector<TableWithOneHandle>:8 sv;
};

table TableWithHandleStructVector {
  1: vector<OneHandle>:8 sv;
};

    )FIDL");
    ASSERT_TRUE(test_library.Compile());

    auto handle_vector = test_library.LookupStruct("HandleVector");
    ASSERT_NONNULL(handle_vector);
    EXPECT_TRUE(CheckTypeShape(handle_vector->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 32,
        .max_handles = 8,
        .depth = 1,
    }));

    auto handle_nullable_vector = test_library.LookupStruct("HandleNullableVector");
    ASSERT_NONNULL(handle_nullable_vector);
    EXPECT_TRUE(CheckTypeShape(handle_nullable_vector->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 32,
        .max_handles = 8,
        .depth = 1,
    }));

    auto unbounded_handle_vector = test_library.LookupStruct("UnboundedHandleVector");
    ASSERT_NONNULL(unbounded_handle_vector);
    EXPECT_TRUE(CheckTypeShape(unbounded_handle_vector->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = std::numeric_limits<uint32_t>::max(),
        .max_handles = std::numeric_limits<uint32_t>::max(),
        .depth = 1,
    }));

    auto table_with_unbounded_handle_vector = test_library.LookupTable("TableWithUnboundedHandleVector");
    ASSERT_NONNULL(table_with_unbounded_handle_vector);
    EXPECT_TRUE(CheckTypeShape(table_with_unbounded_handle_vector->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = std::numeric_limits<uint32_t>::max(),
        .max_handles = std::numeric_limits<uint32_t>::max(),
        .depth = 4,  // TODO(FIDL-457): wrong.
    }));

    auto handle_struct_vector = test_library.LookupStruct("HandleStructVector");
    ASSERT_NONNULL(handle_struct_vector);
    EXPECT_TRUE(CheckTypeShape(handle_struct_vector->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 32,
        .max_handles = 8,
        .depth = 1,
    }));

    auto handle_table_vector = test_library.LookupStruct("HandleTableVector");
    ASSERT_NONNULL(handle_table_vector);
    EXPECT_TRUE(CheckTypeShape(handle_table_vector->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 320,
        .max_handles = 8,
        .depth = 4, // TODO(FIDL-457): wrong.
    }));

    auto table_with_handle_struct_vector = test_library.LookupTable("TableWithHandleStructVector");
    ASSERT_NONNULL(table_with_handle_struct_vector);
    EXPECT_TRUE(CheckTypeShape(table_with_handle_struct_vector->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 64,
        .max_handles = 8,
        .depth = 4, // TODO(FIDL-457): wrong.
    }));

    END_TEST;
}

static bool strings() {
    BEGIN_TEST;

    TestLibrary test_library(R"FIDL(
library example;

struct ShortString {
  string:5 s;
};

struct UnboundedString {
  string s;
};

table TableWithShortString {
  1: string:5 s;
};

table TableWithUnboundedString {
  1: string s;
};

    )FIDL");
    ASSERT_TRUE(test_library.Compile());

    auto short_string = test_library.LookupStruct("ShortString");
    ASSERT_NONNULL(short_string);
    EXPECT_TRUE(CheckTypeShape(short_string->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 8,
        .depth = 1,
    }));

    auto unbounded_string = test_library.LookupStruct("UnboundedString");
    ASSERT_NONNULL(unbounded_string);
    EXPECT_TRUE(CheckTypeShape(unbounded_string->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = std::numeric_limits<uint32_t>::max(),
        .depth = 1,
    }));

    auto table_with_short_string = test_library.LookupTable("TableWithShortString");
    ASSERT_NONNULL(table_with_short_string);
    EXPECT_TRUE(CheckTypeShape(table_with_short_string->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 40,
        .depth = 4, // TODO(FIDL-457): wrong.
    }));

    auto table_with_unbounded_string = test_library.LookupTable("TableWithUnboundedString");
    ASSERT_NONNULL(table_with_unbounded_string);
    EXPECT_TRUE(CheckTypeShape(table_with_unbounded_string->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = std::numeric_limits<uint32_t>::max(),
        .depth = 4, // TODO(FIDL-457): wrong.
    }));

    END_TEST;
}

static bool arrays() {
    BEGIN_TEST;

    TestLibrary test_library(R"FIDL(
library example;

struct AnArray {
  array<int64>:5 a;
};

table TableWithAnArray {
  1: array<int64>:5 a;
};

    )FIDL");
    ASSERT_TRUE(test_library.Compile());

    auto an_array = test_library.LookupStruct("AnArray");
    ASSERT_NONNULL(an_array);
    EXPECT_TRUE(CheckTypeShape(an_array->typeshape, Expected {
        .size = 40,
        .alignment = 8,
    }));

    auto table_with_an_array = test_library.LookupTable("TableWithAnArray");
    ASSERT_NONNULL(table_with_an_array);
    EXPECT_TRUE(CheckTypeShape(table_with_an_array->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 56,
        .depth = 3, // TODO(FIDL-457): wrong.
    }));

    END_TEST;
}

static bool arrays_with_handles() {
    BEGIN_TEST;

    TestLibrary test_library(R"FIDL(
library example;

struct HandleArray {
  array<handle>:8 ha;
};

table TableWithHandleArray {
  1: array<handle>:8 ha;
};

struct NullableHandleArray {
  array<handle?>:8 ha;
};

table TableWithNullableHandleArray {
  1: array<handle?>:8 ha;
};

    )FIDL");
    ASSERT_TRUE(test_library.Compile());

    auto handle_array = test_library.LookupStruct("HandleArray");
    ASSERT_NONNULL(handle_array);
    EXPECT_TRUE(CheckTypeShape(handle_array->typeshape, Expected {
        .size = 32,
        .alignment = 4,
        .max_handles = 8,
    }));

    auto table_with_handle_array = test_library.LookupTable("TableWithHandleArray");
    ASSERT_NONNULL(table_with_handle_array);
    EXPECT_TRUE(CheckTypeShape(table_with_handle_array->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 48,
        .max_handles = 8,
        .depth = 3, // TODO(FIDL-457): wrong.
    }));

    auto nullable_handle_array = test_library.LookupStruct("NullableHandleArray");
    ASSERT_NONNULL(nullable_handle_array);
    EXPECT_TRUE(CheckTypeShape(nullable_handle_array->typeshape, Expected {
        .size = 32,
        .alignment = 4,
        .max_handles = 8,
    }));

    auto table_with_nullable_handle_array = test_library.LookupTable("TableWithNullableHandleArray");
    ASSERT_NONNULL(table_with_nullable_handle_array);
    EXPECT_TRUE(CheckTypeShape(table_with_nullable_handle_array->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 48,
        .max_handles = 8,
        .depth = 3, // TODO(FIDL-457): wrong.
    }));

    END_TEST;
}

static bool xunions() {
    BEGIN_TEST;

    TestLibrary test_library(R"FIDL(
library example;

xunion EmptyXUnion {
};

xunion XUnionWithOneBool {
  bool b;
};

xunion XUnionWithBoundedOutOfLineObject {
  // smaller than |v| below, so will not be selected for max-out-of-line
  // calculation.
  bool b;

  // 1. vector<int32>:5 = 8 bytes for vector element count
  //                    + 8 bytes for data pointer
  //                    + 24 bytes out-of-line (20 bytes contents +
  //                                            4 bytes for 8-byte alignment)
  //                    = 40 bytes total
  // 1. vector<vector<int32>:5>:6 = vector of up to six of vector<int32>:5
  //                              = 8 bytes for vector element count
  //                              + 8 bytes for data pointer
  //                              + 240 bytes out-of-line (40 bytes contents * 6)
  //                              = 256 bytes total
  vector<vector<int32>:5>:6 v;
};

xunion XUnionWithUnboundedOutOfLineObject {
  string s;
};

struct StructWithOptionalEmptyXUnion {
  EmptyXUnion? opt_empty;
};

    )FIDL");
    ASSERT_TRUE(test_library.Compile());

    auto empty = test_library.LookupXUnion("EmptyXUnion");
    ASSERT_NONNULL(empty);
    EXPECT_TRUE(CheckTypeShape(empty->typeshape, Expected {
        .size = 24,
        .alignment = 8,
        .max_out_of_line = 0,
        .depth = 0, // TODO(FIDL-457): wrong.
    }));

    auto one_bool = test_library.LookupXUnion("XUnionWithOneBool");
    ASSERT_NONNULL(one_bool);
    EXPECT_TRUE(CheckTypeShape(one_bool->typeshape, Expected {
        .size = 24,
        .alignment = 8,
        .max_out_of_line = 8,
        .depth = 1, // TODO(FIDL-457): wrong.
    }));

    auto xu = test_library.LookupXUnion("XUnionWithBoundedOutOfLineObject");
    ASSERT_NONNULL(xu);
    EXPECT_TRUE(CheckTypeShape(xu->typeshape, Expected {
        .size = 24,
        .alignment = 8,
        .max_out_of_line = 256,
        .depth = 4, // TODO(FIDL-457): wrong.
    }));

    auto unbounded = test_library.LookupXUnion("XUnionWithUnboundedOutOfLineObject");
    ASSERT_NONNULL(unbounded);
    EXPECT_TRUE(CheckTypeShape(unbounded->typeshape, Expected {
        .size = 24,
        .alignment = 8,
        .max_out_of_line = std::numeric_limits<uint32_t>::max(),
        .depth = 2, // TODO(FIDL-457): wrong.
    }));

    auto opt_empty = test_library.LookupStruct("StructWithOptionalEmptyXUnion");
    ASSERT_NONNULL(opt_empty);
    EXPECT_TRUE(CheckTypeShape(opt_empty->typeshape, Expected {
        .size = 24,
        .alignment = 8,
        .max_out_of_line = 0,
        .depth = 0, // TODO(FIDL-457): wrong.
    }));

    END_TEST;
}

bool protocols_and_request_of_protocols() {
    BEGIN_TEST;

    TestLibrary test_library(R"FIDL(
library example;

protocol SomeProtocol {};

struct UsingSomeProtocol {
  SomeProtocol value;
};

struct UsingOptSomeProtocol {
  SomeProtocol? value;
};

struct UsingRequestSomeProtocol {
  request<SomeProtocol> value;
};

struct UsingOptRequestSomeProtocol {
  request<SomeProtocol>? value;
};

    )FIDL");
    ASSERT_TRUE(test_library.Compile());

    auto using_some_protocol = test_library.LookupStruct("UsingSomeProtocol");
    ASSERT_NONNULL(using_some_protocol);
    EXPECT_TRUE(CheckTypeShape(using_some_protocol->typeshape, Expected {
        .size = 4,
        .alignment = 4,
        .max_handles = 1,
    }));

    auto using_opt_some_protocol = test_library.LookupStruct("UsingOptSomeProtocol");
    ASSERT_NONNULL(using_opt_some_protocol);
    EXPECT_TRUE(CheckTypeShape(using_opt_some_protocol->typeshape, Expected {
        .size = 4,
        .alignment = 4,
        .max_handles = 1,
    }));

    auto using_request_some_protocol = test_library.LookupStruct("UsingRequestSomeProtocol");
    ASSERT_NONNULL(using_request_some_protocol);
    EXPECT_TRUE(CheckTypeShape(using_request_some_protocol->typeshape, Expected {
        .size = 4,
        .alignment = 4,
        .max_handles = 1,
    }));

    auto using_opt_request_some_protocol = test_library.LookupStruct("UsingOptRequestSomeProtocol");
    ASSERT_NONNULL(using_opt_request_some_protocol);
    EXPECT_TRUE(CheckTypeShape(using_opt_request_some_protocol->typeshape, Expected {
        .size = 4,
        .alignment = 4,
        .max_handles = 1,
    }));

    END_TEST;
}

bool external_definitions() {
    BEGIN_TEST;

    auto test_library = TestLibrary();
    test_library.AddSource("main.fidl", R"FIDL(
library example;

struct ExternalArrayStruct {
    array<ExternalSimpleStruct>:EXTERNAL_SIZE_DEF a;
};

struct ExternalStringSizeStruct {
    string:EXTERNAL_SIZE_DEF a;
};

struct ExternalVectorSizeStruct {
    vector<handle>:EXTERNAL_SIZE_DEF a;
};

    )FIDL");
    test_library.AddSource("extern_defs.fidl", R"FIDL(
library example;

const uint32 EXTERNAL_SIZE_DEF = ANOTHER_INDIRECTION;
const uint32 ANOTHER_INDIRECTION = 32;

struct ExternalSimpleStruct {
    uint32 a;
};

    )FIDL");
    ASSERT_TRUE(test_library.Compile());

    auto ext_struct = test_library.LookupStruct("ExternalSimpleStruct");
    ASSERT_NONNULL(ext_struct);
    EXPECT_TRUE(CheckTypeShape(ext_struct->typeshape, Expected {
        .size = 4,
        .alignment = 4,
    }));

    auto ext_arr_struct = test_library.LookupStruct("ExternalArrayStruct");
    ASSERT_NONNULL(ext_arr_struct);
    EXPECT_TRUE(CheckTypeShape(ext_arr_struct->typeshape, Expected {
        .size = 4 * 32,
        .alignment = 4,
    }));

    auto ext_str_struct = test_library.LookupStruct("ExternalStringSizeStruct");
    ASSERT_NONNULL(ext_str_struct);
    EXPECT_TRUE(CheckTypeShape(ext_str_struct->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 32,
        .depth = 1,
    }));

    auto ext_vec_struct = test_library.LookupStruct("ExternalVectorSizeStruct");
    ASSERT_NONNULL(ext_vec_struct);
    EXPECT_TRUE(CheckTypeShape(ext_vec_struct->typeshape, Expected {
        .size = 16,
        .alignment = 8,
        .max_out_of_line = 32 * 4,
        .max_handles = 32,
        .depth = 1,
    }));

    END_TEST;
}

bool recursive_request() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct WebMessage {
  request<MessagePort> message_port_req;
};

protocol MessagePort {
  PostMessage(WebMessage message) -> (bool success);
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto web_message = library.LookupStruct("WebMessage");
  ASSERT_NONNULL(web_message);
  EXPECT_TRUE(CheckTypeShape(web_message->typeshape, Expected {
      .size = 4,
      .alignment = 4,
      .max_handles = 1,
  }));

  auto message_port = library.LookupInterface("MessagePort");
  ASSERT_NONNULL(message_port);
  ASSERT_EQ(message_port->methods.size(), 1);
  auto& post_message = message_port->methods[0];
  auto post_message_request = post_message.maybe_request;
  ASSERT_NONNULL(post_message_request);
  EXPECT_TRUE(CheckTypeShape(post_message_request->typeshape, Expected {
      .size = 24,
      .alignment = 8,
      .max_handles = 1,
  }));

  END_TEST;
}

bool recursive_opt_request() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct WebMessage {
  request<MessagePort>? opt_message_port_req;
};

protocol MessagePort {
  PostMessage(WebMessage message) -> (bool success);
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto web_message = library.LookupStruct("WebMessage");
  ASSERT_NONNULL(web_message);
  EXPECT_TRUE(CheckTypeShape(web_message->typeshape, Expected {
      .size = 4,
      .alignment = 4,
      .max_handles = 1,
  }));

  auto message_port = library.LookupInterface("MessagePort");
  ASSERT_NONNULL(message_port);
  ASSERT_EQ(message_port->methods.size(), 1);
  auto& post_message = message_port->methods[0];
  auto post_message_request = post_message.maybe_request;
  ASSERT_NONNULL(post_message_request);
  EXPECT_TRUE(CheckTypeShape(post_message_request->typeshape, Expected {
      .size = 24,
      .alignment = 8,
      .max_handles = 1,
  }));

  END_TEST;
}

bool recursive_protocol() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct WebMessage {
  MessagePort message_port;
};

protocol MessagePort {
  PostMessage(WebMessage message) -> (bool success);
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto web_message = library.LookupStruct("WebMessage");
  ASSERT_NONNULL(web_message);
  EXPECT_TRUE(CheckTypeShape(web_message->typeshape, Expected {
      .size = 4,
      .alignment = 4,
      .max_handles = 1,
  }));

  auto message_port = library.LookupInterface("MessagePort");
  ASSERT_NONNULL(message_port);
  ASSERT_EQ(message_port->methods.size(), 1);
  auto& post_message = message_port->methods[0];
  auto post_message_request = post_message.maybe_request;
  ASSERT_NONNULL(post_message_request);
  EXPECT_TRUE(CheckTypeShape(post_message_request->typeshape, Expected {
      .size = 24,
      .alignment = 8,
      .max_handles = 1,
  }));

  END_TEST;
}

bool recursive_opt_protocol() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct WebMessage {
  MessagePort? opt_message_port;
};

protocol MessagePort {
  PostMessage(WebMessage message) -> (bool success);
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto web_message = library.LookupStruct("WebMessage");
  ASSERT_NONNULL(web_message);
  EXPECT_TRUE(CheckTypeShape(web_message->typeshape, Expected {
      .size = 4,
      .alignment = 4,
      .max_handles = 1,
  }));

  auto message_port = library.LookupInterface("MessagePort");
  ASSERT_NONNULL(message_port);
  ASSERT_EQ(message_port->methods.size(), 1);
  auto& post_message = message_port->methods[0];
  auto post_message_request = post_message.maybe_request;
  ASSERT_NONNULL(post_message_request);
  EXPECT_TRUE(CheckTypeShape(post_message_request->typeshape, Expected {
      .size = 24,
      .alignment = 8,
      .max_handles = 1,
  }));

  END_TEST;
}

bool recursive_struct() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct TheStruct {
  TheStruct? opt_one_more;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto the_struct = library.LookupStruct("TheStruct");
  ASSERT_NONNULL(the_struct);
  EXPECT_TRUE(CheckTypeShape(the_struct->typeshape, Expected {
      .size = 8,
      .alignment = 8,
      // TODO(FIDL-457): Imprecision here, max out-of-line should be infinite.
      .max_out_of_line = 0,
      // TODO(FIDL-457): Incorrectly saturating, there are no handles here.
      .max_handles = std::numeric_limits<uint32_t>::max(),
      .depth = std::numeric_limits<uint32_t>::max(),
  }));

  END_TEST;
}

bool recursive_struct_with_handles() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct TheStruct {
  handle<vmo> some_handle;
  TheStruct? opt_one_more;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto the_struct = library.LookupStruct("TheStruct");
  ASSERT_NONNULL(the_struct);
  EXPECT_TRUE(CheckTypeShape(the_struct->typeshape, Expected {
      .size = 16,
      .alignment = 8,
      // TODO(FIDL-457): Imprecision here, max out-of-line should be infinite.
      .max_out_of_line = 0,
      .max_handles = std::numeric_limits<uint32_t>::max(),
      .depth = std::numeric_limits<uint32_t>::max(),
  }));

  END_TEST;
}

bool co_recursive_struct() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct A {
    B? foo;
};

struct B {
    A? bar;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto struct_a = library.LookupStruct("A");
  ASSERT_NONNULL(struct_a);
  EXPECT_TRUE(CheckTypeShape(struct_a->typeshape, Expected {
      .size = 8,
      .alignment = 8,
      // TODO(FIDL-457): Imprecision here, max out-of-line should be infinite.
      .max_out_of_line = 16,
      // TODO(FIDL-457): Incorrectly saturating, there are no handles here.
      .max_handles = std::numeric_limits<uint32_t>::max(),
      .depth = std::numeric_limits<uint32_t>::max(),
  }));

  auto struct_b = library.LookupStruct("B");
  ASSERT_NONNULL(struct_b);
  EXPECT_TRUE(CheckTypeShape(struct_b->typeshape, Expected {
      .size = 8,
      .alignment = 8,
      // TODO(FIDL-457): Imprecision here, max out-of-line should be infinite.
      .max_out_of_line = 8,
      // TODO(FIDL-457): Incorrectly saturating, there are no handles here.
      .max_handles = std::numeric_limits<uint32_t>::max(),
      .depth = std::numeric_limits<uint32_t>::max(),
  }));

  END_TEST;
}

bool co_recursive_struct_with_handles() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct A {
    handle a;
    B? foo;
};

struct B {
    handle b;
    A? bar;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto struct_a = library.LookupStruct("A");
  ASSERT_NONNULL(struct_a);
  EXPECT_TRUE(CheckTypeShape(struct_a->typeshape, Expected {
      .size = 16,
      .alignment = 8,
      // TODO(FIDL-457): Imprecision here, max out-of-line should be infinite.
      .max_out_of_line = 32,
      .max_handles = std::numeric_limits<uint32_t>::max(),
      .depth = std::numeric_limits<uint32_t>::max(),
  }));

  auto struct_b = library.LookupStruct("B");
  ASSERT_NONNULL(struct_b);
  EXPECT_TRUE(CheckTypeShape(struct_b->typeshape, Expected {
      .size = 16,
      .alignment = 8,
      // TODO(FIDL-457): Imprecision here, max out-of-line should be infinite.
      .max_out_of_line = 16,
      .max_handles = std::numeric_limits<uint32_t>::max(),
      .depth = std::numeric_limits<uint32_t>::max(),
  }));

  END_TEST;
}

bool co_recursive_struct2() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct Foo {
    Bar b;
};

struct Bar {
    Foo? f;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto struct_foo = library.LookupStruct("Foo");
  ASSERT_NONNULL(struct_foo);
  EXPECT_TRUE(CheckTypeShape(struct_foo->typeshape, Expected {
      .size = 8,
      .alignment = 8,
      // TODO(FIDL-457): Imprecision here, max out-of-line should be infinite.
      .max_out_of_line = 0,
      // TODO(FIDL-457): Incorrectly saturating, there are no handles here.
      .max_handles = std::numeric_limits<uint32_t>::max(),
      .depth = std::numeric_limits<uint32_t>::max(),
  }));

  auto struct_bar = library.LookupStruct("Bar");
  ASSERT_NONNULL(struct_bar);
  EXPECT_TRUE(CheckTypeShape(struct_bar->typeshape, Expected {
      .size = 8,
      .alignment = 8,
      // TODO(FIDL-457): Imprecision here, max out-of-line should be infinite.
      .max_out_of_line = 0,
      // TODO(FIDL-457): Incorrectly saturating, there are no handles here.
      .max_handles = std::numeric_limits<uint32_t>::max(),
      .depth = std::numeric_limits<uint32_t>::max(),
  }));

  END_TEST;
}

bool struct_two_deep() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct DiffEntry {
    vector<uint8>:256 key;

    Value? base;
    Value? left;
    Value? right;
};

struct Value {
    Buffer? value;
    Priority priority;
};

struct Buffer {
    handle<vmo> vmo;
    uint64 size;
};

enum Priority {
    EAGER = 0;
    LAZY = 1;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto buffer = library.LookupStruct("Buffer");
  ASSERT_NONNULL(buffer);
  EXPECT_TRUE(CheckTypeShape(buffer->typeshape, Expected {
      .size = 16,
      .alignment = 8,
      .max_handles = 1,
  }));

  auto value = library.LookupStruct("Value");
  ASSERT_NONNULL(value);
  EXPECT_TRUE(CheckTypeShape(value->typeshape, Expected {
      .size = 16,
      .alignment = 8,
      .max_out_of_line = 16,
      .max_handles = 1,
      .depth = 1,
  }));

  auto diff_entry = library.LookupStruct("DiffEntry");
  ASSERT_NONNULL(diff_entry);
  EXPECT_TRUE(CheckTypeShape(diff_entry->typeshape, Expected {
      .size = 40,
      .alignment = 8,
      .max_out_of_line = 352,
      .max_handles = 3,
      .depth = 2,
  }));

  END_TEST;
}

bool protocol_child_and_parent() {
  BEGIN_TEST;

  SharedAmongstLibraries shared;
  TestLibrary parent_library("parent.fidl", R"FIDL(
library parent;

[FragileBase]
protocol Parent {
  Sync() -> ();
};
)FIDL", &shared);
  ASSERT_TRUE(parent_library.Compile());

  TestLibrary child_library("child.fidl", R"FIDL(
library child;

using parent;

protocol Child {
  compose parent.Parent;
};
)FIDL", &shared);
  ASSERT_TRUE(child_library.AddDependentLibrary(std::move(parent_library)));
  ASSERT_TRUE(child_library.Compile());

  auto child = child_library.LookupInterface("Child");
  ASSERT_NONNULL(child);
  ASSERT_EQ(child->all_methods.size(), 1);
  auto& sync = child->all_methods[0];
  auto sync_request = sync->maybe_request;
  ASSERT_NONNULL(sync_request);
  EXPECT_TRUE(CheckTypeShape(sync_request->typeshape, Expected {
      .size = 16,
      .alignment = 8,
  }));

  END_TEST;
}

} // namespace

BEGIN_TEST_CASE(typeshape_tests)
RUN_TEST(simple_structs)
RUN_TEST(simple_structs_with_handles)
RUN_TEST(simple_tables)
RUN_TEST(simple_tables_with_handles)
RUN_TEST(optional_structs)
RUN_TEST(optional_tables)
RUN_TEST(unions)
RUN_TEST(unions_with_handles)
RUN_TEST(vectors)
RUN_TEST(vectors_with_handles)
RUN_TEST(strings)
RUN_TEST(arrays)
RUN_TEST(arrays_with_handles)
RUN_TEST(xunions)
// RUN_TEST(xunions_with_handles) TODO(pascallouis)
RUN_TEST(protocols_and_request_of_protocols)
RUN_TEST(external_definitions)
RUN_TEST(recursive_request)
RUN_TEST(recursive_opt_request)
RUN_TEST(recursive_protocol)
RUN_TEST(recursive_opt_protocol)
RUN_TEST(recursive_struct)
RUN_TEST(recursive_struct_with_handles)
RUN_TEST(co_recursive_struct)
RUN_TEST(co_recursive_struct_with_handles)
RUN_TEST(co_recursive_struct2)
RUN_TEST(struct_two_deep)
RUN_TEST(protocol_child_and_parent)
END_TEST_CASE(typeshape_tests)
