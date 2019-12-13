// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>
#include <fidl/type_shape.h>
#include <unittest/unittest.h>

#include "test_library.h"

namespace {

struct Expected {
  uint32_t inline_size = 0;
  uint32_t alignment = 0;
  uint32_t max_out_of_line = 0;
  uint32_t max_handles = 0;
  uint32_t depth = 0;
  bool has_padding = false;
  bool has_flexible_envelope = false;
  bool contains_union = false;
};

bool CheckTypeShape(const fidl::TypeShape& actual, Expected expected) {
  BEGIN_HELPER;
  EXPECT_EQ(actual.InlineSize(), expected.inline_size);
  EXPECT_EQ(actual.Alignment(), expected.alignment);
  EXPECT_EQ(actual.MaxOutOfLine(), expected.max_out_of_line);
  EXPECT_EQ(actual.MaxHandles(), expected.max_handles);
  EXPECT_EQ(actual.Depth(), expected.depth);
  EXPECT_EQ(actual.HasPadding(), expected.has_padding);
  EXPECT_EQ(actual.HasFlexibleEnvelope(), expected.has_flexible_envelope);
  EXPECT_EQ(actual.ContainsUnion(), expected.contains_union);
  END_HELPER;
}

bool CheckTypeShape(const fidl::flat::Object* actual, Expected expected_old,
                    Expected expected_v1_no_ee) {
  if (!CheckTypeShape(fidl::TypeShape(actual, fidl::WireFormat::kOld), expected_old)) {
    return false;
  }
  if (!CheckTypeShape(fidl::TypeShape(actual, fidl::WireFormat::kV1NoEe), expected_v1_no_ee)) {
    return false;
  }
  return true;
}

bool CheckTypeShape(const fidl::flat::Object* actual, Expected expected) {
  return CheckTypeShape(actual, expected, expected);
}

bool CheckContainsUnion(const fidl::flat::Object* object, bool expected) {
  // contains_union will be the same for both wire formats, just check v1
  auto typeshape = fidl::TypeShape(object, fidl::WireFormat::kV1NoEe);
  BEGIN_HELPER;
  EXPECT_EQ(typeshape.ContainsUnion(), expected);
  END_HELPER;
}

struct ExpectedField {
  uint32_t offset = 0;
  uint32_t padding = 0;
};

template <typename T>
bool CheckFieldShape(const T& field, ExpectedField expected_old, ExpectedField expected_v1) {
  BEGIN_HELPER;

  const fidl::FieldShape& actual_old = fidl::FieldShape(field, fidl::WireFormat::kOld);
  EXPECT_EQ(actual_old.offset, expected_old.offset);
  EXPECT_EQ(actual_old.padding, expected_old.padding);

  const fidl::FieldShape& actual_v1 = fidl::FieldShape(field, fidl::WireFormat::kV1NoEe);
  EXPECT_EQ(actual_v1.offset, expected_v1.offset);
  EXPECT_EQ(actual_v1.padding, expected_v1.padding);

  END_HELPER;
}

template <typename T>
bool CheckFieldShape(const T& field, ExpectedField expected_old) {
  return CheckFieldShape(field, expected_old, expected_old);
}

static bool empty_struct() {
  BEGIN_TEST;

  TestLibrary test_library(R"FIDL(
library example;

struct Empty {};

    )FIDL");
  ASSERT_TRUE(test_library.Compile());

  auto empty = test_library.LookupStruct("Empty");
  ASSERT_NONNULL(empty);
  EXPECT_TRUE(CheckTypeShape(empty, Expected{
                                        .inline_size = 1,
                                        .alignment = 1,
                                    }));
  ASSERT_EQ(empty->members.size(), 0);

  END_TEST;
}

static bool empty_struct_within_another_struct() {
  BEGIN_TEST;

  TestLibrary test_library(R"FIDL(
library example;

struct Empty {};

// Size = 1 byte for |bool a|
//      + 1 byte for |Empty b|
//      + 2 bytes for |int16 c|
//      + 1 bytes for |Empty d|
//      + 3 bytes padding
//      + 4 bytes for |int32 e|
//      + 2 bytes for |int16 f|
//      + 1 byte for |Empty g|
//      + 1 byte for |Empty h|
//      = 16 bytes
//
// Alignment = 4 bytes stemming from largest member (int32).
//
struct EmptyWithOtherThings {
  bool a;
  // no padding
  Empty b;
  // no padding
  int16 c;
  // no padding
  Empty d;
  // 3 bytes padding
  int32 e;
  // no padding
  int16 f;
  // no padding
  Empty g;
  // no padding
  Empty h;
};

    )FIDL");
  ASSERT_TRUE(test_library.Compile());

  auto empty_with_other_things = test_library.LookupStruct("EmptyWithOtherThings");
  ASSERT_NONNULL(empty_with_other_things);
  EXPECT_TRUE(CheckTypeShape(empty_with_other_things, Expected{
                                                          .inline_size = 16,
                                                          .alignment = 4,
                                                          .has_padding = true,
                                                      }));
  ASSERT_EQ(empty_with_other_things->members.size(), 8);
  // bool a;
  EXPECT_TRUE(CheckFieldShape(empty_with_other_things->members[0], ExpectedField{}));
  // Empty b;
  EXPECT_TRUE(CheckFieldShape(empty_with_other_things->members[1], ExpectedField{
                                                                       .offset = 1,
                                                                   }));
  // int16 c;
  EXPECT_TRUE(CheckFieldShape(empty_with_other_things->members[2], ExpectedField{
                                                                       .offset = 2,
                                                                   }));
  // Empty d;
  EXPECT_TRUE(CheckFieldShape(empty_with_other_things->members[3],
                              ExpectedField{.offset = 4, .padding = 3}));
  // int32 e;
  EXPECT_TRUE(CheckFieldShape(empty_with_other_things->members[4], ExpectedField{
                                                                       .offset = 8,
                                                                   }));
  // int16 f;
  EXPECT_TRUE(CheckFieldShape(empty_with_other_things->members[5], ExpectedField{
                                                                       .offset = 12,
                                                                   }));
  // Empty g;
  EXPECT_TRUE(CheckFieldShape(empty_with_other_things->members[6], ExpectedField{
                                                                       .offset = 14,
                                                                   }));
  // Empty h;
  EXPECT_TRUE(CheckFieldShape(empty_with_other_things->members[7], ExpectedField{
                                                                       .offset = 15,
                                                                   }));

  END_TEST;
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
  EXPECT_TRUE(CheckTypeShape(one_bool, Expected{
                                           .inline_size = 1,
                                           .alignment = 1,
                                       }));
  ASSERT_EQ(one_bool->members.size(), 1);
  EXPECT_TRUE(CheckFieldShape(one_bool->members[0], ExpectedField{}));

  auto two_bools = test_library.LookupStruct("TwoBools");
  ASSERT_NONNULL(two_bools);
  EXPECT_TRUE(CheckTypeShape(two_bools, Expected{
                                            .inline_size = 2,
                                            .alignment = 1,
                                        }));
  ASSERT_EQ(two_bools->members.size(), 2);
  EXPECT_TRUE(CheckFieldShape(two_bools->members[0], ExpectedField{}));
  EXPECT_TRUE(CheckFieldShape(two_bools->members[1], ExpectedField{
                                                         .offset = 1,
                                                     }));

  auto bool_and_u32 = test_library.LookupStruct("BoolAndU32");
  ASSERT_NONNULL(bool_and_u32);
  EXPECT_TRUE(CheckTypeShape(bool_and_u32, Expected{
                                               .inline_size = 8,
                                               .alignment = 4,
                                               .has_padding = true,
                                           }));
  ASSERT_EQ(bool_and_u32->members.size(), 2);
  EXPECT_TRUE(CheckFieldShape(bool_and_u32->members[0], ExpectedField{.padding = 3}));
  EXPECT_TRUE(CheckFieldShape(bool_and_u32->members[1], ExpectedField{
                                                            .offset = 4,
                                                        }));

  auto bool_and_u64 = test_library.LookupStruct("BoolAndU64");
  ASSERT_NONNULL(bool_and_u64);
  EXPECT_TRUE(CheckTypeShape(bool_and_u64, Expected{
                                               .inline_size = 16,
                                               .alignment = 8,
                                               .has_padding = true,
                                           }));
  ASSERT_EQ(bool_and_u64->members.size(), 2);
  EXPECT_TRUE(CheckFieldShape(bool_and_u64->members[0], ExpectedField{.padding = 7}));
  EXPECT_TRUE(CheckFieldShape(bool_and_u64->members[1], ExpectedField{
                                                            .offset = 8,
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
  EXPECT_TRUE(CheckTypeShape(one_handle, Expected{
                                             .inline_size = 4,
                                             .alignment = 4,
                                             .max_handles = 1,
                                         }));
  ASSERT_EQ(one_handle->members.size(), 1);
  EXPECT_TRUE(CheckFieldShape(one_handle->members[0], ExpectedField{}));

  auto two_handles = test_library.LookupStruct("TwoHandles");
  ASSERT_NONNULL(two_handles);
  EXPECT_TRUE(CheckTypeShape(two_handles, Expected{
                                              .inline_size = 8,
                                              .alignment = 4,
                                              .max_handles = 2,
                                          }));
  ASSERT_EQ(two_handles->members.size(), 2);
  EXPECT_TRUE(CheckFieldShape(two_handles->members[0], ExpectedField{}));
  EXPECT_TRUE(CheckFieldShape(two_handles->members[1], ExpectedField{
                                                           .offset = 4,
                                                       }));

  auto three_handles_one_optional = test_library.LookupStruct("ThreeHandlesOneOptional");
  ASSERT_NONNULL(three_handles_one_optional);
  EXPECT_TRUE(CheckTypeShape(three_handles_one_optional, Expected{
                                                             .inline_size = 12,
                                                             .alignment = 4,
                                                             .max_handles = 3,
                                                         }));
  ASSERT_EQ(three_handles_one_optional->members.size(), 3);
  EXPECT_TRUE(CheckFieldShape(three_handles_one_optional->members[0], ExpectedField{}));
  EXPECT_TRUE(CheckFieldShape(three_handles_one_optional->members[1], ExpectedField{
                                                                          .offset = 4,
                                                                      }));
  EXPECT_TRUE(CheckFieldShape(three_handles_one_optional->members[2], ExpectedField{
                                                                          .offset = 8,
                                                                      }));

  END_TEST;
}

bool bits() {
  BEGIN_TEST;

  TestLibrary test_library(R"FIDL(
library example;

bits Bits16 : uint16 {
    VALUE = 1;
};

bits BitsImplicit {
    VALUE = 1;
};
)FIDL");
  ASSERT_TRUE(test_library.Compile());

  auto bits16 = test_library.LookupBits("Bits16");
  ASSERT_NONNULL(bits16);
  EXPECT_TRUE(CheckTypeShape(bits16, Expected{
                                         .inline_size = 2,
                                         .alignment = 2,
                                     }));

  auto bits_implicit = test_library.LookupBits("BitsImplicit");
  EXPECT_NONNULL(bits_implicit);
  EXPECT_TRUE(CheckTypeShape(bits_implicit, Expected{
                                                .inline_size = 4,
                                                .alignment = 4,
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
  EXPECT_TRUE(CheckTypeShape(no_members, Expected{
                                             .inline_size = 16,
                                             .alignment = 8,
                                             .depth = 1,
                                             .has_padding = false,
                                             .has_flexible_envelope = true,
                                         }));

  auto one_bool = test_library.LookupTable("TableWithOneBool");
  ASSERT_NONNULL(one_bool);
  EXPECT_TRUE(CheckTypeShape(one_bool, Expected{
                                           .inline_size = 16,
                                           .alignment = 8,
                                           .max_out_of_line = 24,
                                           .depth = 2,
                                           .has_padding = true,
                                           .has_flexible_envelope = true,
                                       }));

  auto two_bools = test_library.LookupTable("TableWithTwoBools");
  ASSERT_NONNULL(two_bools);
  EXPECT_TRUE(CheckTypeShape(two_bools, Expected{
                                            .inline_size = 16,
                                            .alignment = 8,
                                            .max_out_of_line = 48,
                                            .depth = 2,
                                            .has_padding = true,
                                            .has_flexible_envelope = true,
                                        }));

  auto bool_and_u32 = test_library.LookupTable("TableWithBoolAndU32");
  ASSERT_NONNULL(bool_and_u32);
  EXPECT_TRUE(CheckTypeShape(bool_and_u32, Expected{
                                               .inline_size = 16,
                                               .alignment = 8,
                                               .max_out_of_line = 48,
                                               .depth = 2,
                                               .has_padding = true,
                                               .has_flexible_envelope = true,
                                           }));

  auto bool_and_u64 = test_library.LookupTable("TableWithBoolAndU64");
  ASSERT_NONNULL(bool_and_u64);
  EXPECT_TRUE(CheckTypeShape(bool_and_u32, Expected{
                                               .inline_size = 16,
                                               .alignment = 8,
                                               .max_out_of_line = 48,
                                               .depth = 2,
                                               .has_padding = true,
                                               .has_flexible_envelope = true,
                                           }));

  END_TEST;
}

static bool tables_with_reserved_fields() {
  BEGIN_TEST;

  TestLibrary test_library(R"FIDL(
library example;

table SomeReserved {
  1: bool b;
  2: reserved;
  3: bool b2;
  4: reserved;
};

table AllReserved {
  1: reserved;
  2: reserved;
  3: reserved;
};

table OneReserved {
  1: reserved;
};
    )FIDL");
  ASSERT_TRUE(test_library.Compile());

  auto some_reserved = test_library.LookupTable("SomeReserved");
  ASSERT_NONNULL(some_reserved);
  EXPECT_TRUE(CheckTypeShape(some_reserved, Expected{
                                                .inline_size = 16,
                                                .alignment = 8,
                                                .max_out_of_line = 48,
                                                .depth = 2,
                                                .has_padding = true,
                                                .has_flexible_envelope = true,
                                            }));

  auto all_reserved = test_library.LookupTable("AllReserved");
  ASSERT_NONNULL(all_reserved);
  EXPECT_TRUE(CheckTypeShape(all_reserved, Expected{
                                               .inline_size = 16,
                                               .alignment = 8,
                                               .max_out_of_line = 0,
                                               .depth = 1,
                                               .has_padding = false,
                                               .has_flexible_envelope = true,
                                           }));

  auto one_reserved = test_library.LookupTable("OneReserved");
  ASSERT_NONNULL(one_reserved);
  EXPECT_TRUE(CheckTypeShape(one_reserved, Expected{
                                               .inline_size = 16,
                                               .alignment = 8,
                                               .max_out_of_line = 0,
                                               .depth = 1,
                                               .has_padding = false,
                                               .has_flexible_envelope = true,
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
  EXPECT_TRUE(CheckTypeShape(one_handle, Expected{
                                             .inline_size = 16,
                                             .alignment = 8,
                                             .max_out_of_line = 24,
                                             .max_handles = 1,
                                             .depth = 2,
                                             .has_padding = true,
                                             .has_flexible_envelope = true,
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
  EXPECT_TRUE(CheckTypeShape(one_bool, Expected{
                                           .inline_size = 8,
                                           .alignment = 8,
                                           .max_out_of_line = 8,
                                           .depth = 1,
                                           .has_padding = true,
                                       }));

  auto two_bools = test_library.LookupStruct("OptionalTwoBools");
  ASSERT_NONNULL(two_bools);
  EXPECT_TRUE(CheckTypeShape(two_bools, Expected{
                                            .inline_size = 8,
                                            .alignment = 8,
                                            .max_out_of_line = 8,
                                            .depth = 1,
                                            .has_padding = true,
                                        }));

  auto bool_and_u32 = test_library.LookupStruct("OptionalBoolAndU32");
  ASSERT_NONNULL(bool_and_u32);
  EXPECT_TRUE(
      CheckTypeShape(bool_and_u32, Expected{
                                       .inline_size = 8,
                                       .alignment = 8,
                                       .max_out_of_line = 8,
                                       .depth = 1,
                                       .has_padding = true,  // because |BoolAndU32| has padding
                                   }));

  auto bool_and_u64 = test_library.LookupStruct("OptionalBoolAndU64");
  ASSERT_NONNULL(bool_and_u64);
  EXPECT_TRUE(
      CheckTypeShape(bool_and_u64, Expected{
                                       .inline_size = 8,
                                       .alignment = 8,
                                       .max_out_of_line = 16,
                                       .depth = 1,
                                       .has_padding = true,  // because |BoolAndU64| has padding
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
  EXPECT_TRUE(CheckTypeShape(one_bool, Expected{
                                           .inline_size = 16,
                                           .alignment = 8,
                                           .max_out_of_line = 24,
                                           .depth = 2,
                                           .has_padding = true,
                                           .has_flexible_envelope = true,
                                       }));

  auto table_with_one_bool = test_library.LookupTable("TableWithOptionalTableWithOneBool");
  ASSERT_NONNULL(table_with_one_bool);
  EXPECT_TRUE(CheckTypeShape(table_with_one_bool, Expected{
                                                      .inline_size = 16,
                                                      .alignment = 8,
                                                      .max_out_of_line = 56,
                                                      .depth = 4,
                                                      .has_padding = true,
                                                      .has_flexible_envelope = true,
                                                  }));

  auto two_bools = test_library.LookupTable("TableWithOptionalTwoBools");
  ASSERT_NONNULL(two_bools);
  EXPECT_TRUE(CheckTypeShape(two_bools, Expected{
                                            .inline_size = 16,
                                            .alignment = 8,
                                            .max_out_of_line = 24,
                                            .depth = 2,
                                            .has_padding = true,
                                            .has_flexible_envelope = true,
                                        }));

  auto table_with_two_bools = test_library.LookupTable("TableWithOptionalTableWithTwoBools");
  ASSERT_NONNULL(table_with_two_bools);
  EXPECT_TRUE(CheckTypeShape(table_with_two_bools, Expected{
                                                       .inline_size = 16,
                                                       .alignment = 8,
                                                       .max_out_of_line = 80,
                                                       .depth = 4,
                                                       .has_padding = true,
                                                       .has_flexible_envelope = true,
                                                   }));

  auto bool_and_u32 = test_library.LookupTable("TableWithOptionalBoolAndU32");
  ASSERT_NONNULL(bool_and_u32);
  EXPECT_TRUE(CheckTypeShape(bool_and_u32, Expected{
                                               .inline_size = 16,
                                               .alignment = 8,
                                               .max_out_of_line = 24,
                                               .depth = 2,
                                               .has_padding = true,
                                               .has_flexible_envelope = true,
                                           }));

  auto table_with_bool_and_u32 = test_library.LookupTable("TableWithOptionalTableWithBoolAndU32");
  ASSERT_NONNULL(table_with_bool_and_u32);
  EXPECT_TRUE(CheckTypeShape(table_with_bool_and_u32, Expected{
                                                          .inline_size = 16,
                                                          .alignment = 8,
                                                          .max_out_of_line = 80,
                                                          .depth = 4,
                                                          .has_padding = true,
                                                          .has_flexible_envelope = true,
                                                      }));

  auto bool_and_u64 = test_library.LookupTable("TableWithOptionalBoolAndU64");
  ASSERT_NONNULL(bool_and_u64);
  EXPECT_TRUE(CheckTypeShape(bool_and_u64, Expected{
                                               .inline_size = 16,
                                               .alignment = 8,
                                               .max_out_of_line = 32,
                                               .depth = 2,
                                               .has_padding = true,
                                               .has_flexible_envelope = true,
                                           }));

  auto table_with_bool_and_u64 = test_library.LookupTable("TableWithOptionalTableWithBoolAndU64");
  ASSERT_NONNULL(table_with_bool_and_u64);
  EXPECT_TRUE(CheckTypeShape(table_with_bool_and_u64, Expected{
                                                          .inline_size = 16,
                                                          .alignment = 8,
                                                          .max_out_of_line = 80,
                                                          .depth = 4,
                                                          .has_padding = true,
                                                          .has_flexible_envelope = true,
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
  1: bool ob;
  2: BoolAndU64 bu;
};

struct Bool {
  bool b;
};

struct OptBool {
  Bool? opt_b;
};

union UnionWithOutOfLine {
  1: OptBool opt_bool;
};

struct OptionalUnion {
  UnionOfThings? u;
};

table TableWithOptionalUnion {
  1: UnionOfThings u;
};

    )FIDL");
  ASSERT_TRUE(test_library.Compile());

  auto union_with_out_of_line = test_library.LookupUnion("UnionWithOutOfLine");
  EXPECT_TRUE(CheckTypeShape(union_with_out_of_line,
                             Expected{
                                 .inline_size = 16,
                                 .alignment = 8,
                                 .max_out_of_line = 8,
                                 .depth = 1,
                                 .has_padding = true,
                                 .contains_union = true,
                             },
                             Expected{
                                 .inline_size = 24,
                                 .alignment = 8,
                                 .max_out_of_line = 16,
                                 .depth = 2,
                                 .has_padding = true,
                                 .contains_union = true,
                             }));

  auto a_union = test_library.LookupUnion("UnionOfThings");
  ASSERT_NONNULL(a_union);
  EXPECT_TRUE(CheckTypeShape(a_union,
                             Expected{
                                 .inline_size = 24,
                                 .alignment = 8,
                                 .has_padding = true,
                                 .contains_union = true,
                             },
                             Expected{
                                 .inline_size = 24,
                                 .alignment = 8,
                                 .max_out_of_line = 16,
                                 .depth = 1,
                                 .has_padding = true,
                                 .contains_union = true,
                             }));
  ASSERT_EQ(a_union->members.size(), 2);
  ASSERT_NONNULL(a_union->members[0].maybe_used);
  EXPECT_TRUE(
      CheckFieldShape(*a_union->members[0].maybe_used,
                      ExpectedField{
                          .offset = 8,
                          .padding = 15  // The other variant, |BoolAndU64|, has a size of 16 bytes.
                      },
                      ExpectedField{
                          .offset = 0,
                          .padding = 7,
                      }));
  ASSERT_NONNULL(a_union->members[1].maybe_used);
  EXPECT_TRUE(CheckFieldShape(*a_union->members[1].maybe_used,
                              ExpectedField{
                                  .offset = 8,
                                  .padding = 0  // This is the biggest variant.
                              },
                              ExpectedField{}));

  auto optional_union = test_library.LookupStruct("OptionalUnion");
  ASSERT_NONNULL(optional_union);
  EXPECT_TRUE(CheckTypeShape(optional_union,
                             Expected{
                                 .inline_size = 8,
                                 .alignment = 8,
                                 .max_out_of_line = 24,
                                 .depth = 1,
                                 .has_padding = true,  // because |UnionOfThings| has padding
                                 .contains_union = true,
                             },
                             Expected{
                                 // because |UnionOfThings| xunion header is inline
                                 .inline_size = 24,
                                 .alignment = 8,
                                 .max_out_of_line = 16,
                                 .depth = 1,
                                 .has_padding = true,
                                 .contains_union = true,
                             }));

  auto table_with_optional_union = test_library.LookupTable("TableWithOptionalUnion");
  ASSERT_NONNULL(table_with_optional_union);
  EXPECT_TRUE(CheckTypeShape(table_with_optional_union,
                             Expected{
                                 .inline_size = 16,
                                 .alignment = 8,
                                 .max_out_of_line = 40,
                                 .depth = 2,
                                 .has_padding = true,
                                 .has_flexible_envelope = true,
                                 .contains_union = true,
                             },
                             Expected{
                                 .inline_size = 16,
                                 .alignment = 8,
                                 .max_out_of_line = 56,
                                 .depth = 3,
                                 .has_padding = true,
                                 .has_flexible_envelope = true,
                                 .contains_union = true,
                             }));

  END_TEST;
}

static bool unions_with_handles() {
  BEGIN_TEST;

  TestLibrary test_library(R"FIDL(
library example;

union OneHandleUnion {
  1: handle one_handle;
  2: bool one_bool;
  3: uint32 one_int;
};

union ManyHandleUnion {
  1: handle one_handle;
  2: array<handle>:8 handle_array;
  3: vector<handle>:8 handle_vector;
};

    )FIDL");
  ASSERT_TRUE(test_library.Compile());

  auto one_handle_union = test_library.LookupUnion("OneHandleUnion");
  ASSERT_NONNULL(one_handle_union);
  EXPECT_TRUE(CheckTypeShape(one_handle_union,
                             Expected{
                                 .inline_size = 8,
                                 .alignment = 4,
                                 .max_handles = 1,
                                 .has_padding = true,
                                 .contains_union = true,
                             },
                             Expected{
                                 .inline_size = 24,
                                 .alignment = 8,
                                 .max_out_of_line = 8,
                                 .max_handles = 1,
                                 .depth = 1,
                                 .has_padding = true,
                                 .contains_union = true,
                             }));
  ASSERT_EQ(one_handle_union->members.size(), 3);
  ASSERT_NONNULL(one_handle_union->members[0].maybe_used);
  EXPECT_TRUE(CheckFieldShape(*one_handle_union->members[0].maybe_used,
                              ExpectedField{
                                  .offset = 4,
                                  .padding = 0  // This is the biggest variant.
                              },
                              ExpectedField{
                                  .offset = 0,
                                  .padding = 4,
                              }));
  ASSERT_NONNULL(one_handle_union->members[1].maybe_used);
  EXPECT_TRUE(CheckFieldShape(*one_handle_union->members[1].maybe_used,
                              ExpectedField{
                                  .offset = 4,
                                  .padding = 3  // The other variants all have size of 4.
                              },
                              ExpectedField{
                                  .offset = 0,
                                  .padding = 7,
                              }));
  ASSERT_NONNULL(one_handle_union->members[2].maybe_used);
  EXPECT_TRUE(CheckFieldShape(*one_handle_union->members[2].maybe_used,
                              ExpectedField{
                                  .offset = 4,
                                  .padding = 0  // This is the biggest variant.
                              },
                              ExpectedField{
                                  .offset = 0,
                                  .padding = 4,
                              }));

  auto many_handle_union = test_library.LookupUnion("ManyHandleUnion");
  ASSERT_NONNULL(many_handle_union);
  EXPECT_TRUE(CheckTypeShape(many_handle_union,
                             Expected{
                                 .inline_size = 40,
                                 .alignment = 8,
                                 .max_out_of_line = 32,
                                 .max_handles = 8,
                                 .depth = 1,
                                 .has_padding = true,
                                 .contains_union = true,
                             },
                             Expected{
                                 .inline_size = 24,
                                 .alignment = 8,
                                 .max_out_of_line = 48,
                                 .max_handles = 8,
                                 .depth = 2,
                                 .has_padding = true,
                                 .contains_union = true,
                             }));
  ASSERT_EQ(many_handle_union->members.size(), 3);
  ASSERT_NONNULL(many_handle_union->members[1].maybe_used);
  EXPECT_TRUE(CheckFieldShape(
      *many_handle_union->members[0].maybe_used,
      ExpectedField{
          .offset = 8,
          .padding = 28  // The biggest variant, |array<handle>:8|, has a size of 32.
      },
      ExpectedField{
          .offset = 0,
          .padding = 4,
      }));
  ASSERT_NONNULL(many_handle_union->members[1].maybe_used);
  EXPECT_TRUE(CheckFieldShape(*many_handle_union->members[1].maybe_used,
                              ExpectedField{
                                  .offset = 8,
                                  .padding = 0  // This is the biggest variant.
                              },
                              ExpectedField{}));
  ASSERT_NONNULL(many_handle_union->members[2].maybe_used);
  EXPECT_TRUE(CheckFieldShape(
      *many_handle_union->members[2].maybe_used,
      ExpectedField{
          .offset = 8,
          .padding = 16  // This biggest variant, |array<handle>:8|, has a size of 32.
      },
      ExpectedField{}));

  END_TEST;
}

static bool vectors() {
  BEGIN_TEST;

  TestLibrary test_library(R"FIDL(
library example;

struct PaddedVector {
  vector<int32>:3 pv;
};

struct NoPaddingVector {
  vector<uint64>:3 npv;
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
  EXPECT_TRUE(CheckTypeShape(padded_vector, Expected{
                                                .inline_size = 16,
                                                .alignment = 8,
                                                .max_out_of_line = 16,
                                                .depth = 1,
                                                .has_padding = true,
                                            }));

  auto no_padding_vector = test_library.LookupStruct("NoPaddingVector");
  ASSERT_NONNULL(no_padding_vector);
  EXPECT_TRUE(CheckTypeShape(no_padding_vector, Expected{
                                                    .inline_size = 16,
                                                    .alignment = 8,
                                                    .max_out_of_line = 24,
                                                    .depth = 1,
                                                    .has_padding = false,
                                                }));

  auto unbounded_vector = test_library.LookupStruct("UnboundedVector");
  ASSERT_NONNULL(unbounded_vector);
  EXPECT_TRUE(
      CheckTypeShape(unbounded_vector, Expected{
                                           .inline_size = 16,
                                           .alignment = 8,
                                           .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                           .depth = 1,
                                           .has_padding = true,
                                       }));

  auto unbounded_vectors = test_library.LookupStruct("UnboundedVectors");
  ASSERT_NONNULL(unbounded_vectors);
  EXPECT_TRUE(
      CheckTypeShape(unbounded_vectors, Expected{
                                            .inline_size = 32,
                                            .alignment = 8,
                                            .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                            .depth = 1,
                                            .has_padding = true,
                                        }));

  auto table_with_padded_vector = test_library.LookupTable("TableWithPaddedVector");
  ASSERT_NONNULL(table_with_padded_vector);
  EXPECT_TRUE(CheckTypeShape(table_with_padded_vector, Expected{
                                                           .inline_size = 16,
                                                           .alignment = 8,
                                                           .max_out_of_line = 48,
                                                           .depth = 3,
                                                           .has_padding = true,
                                                           .has_flexible_envelope = true,
                                                       }));

  auto table_with_unbounded_vector = test_library.LookupTable("TableWithUnboundedVector");
  ASSERT_NONNULL(table_with_unbounded_vector);
  EXPECT_TRUE(CheckTypeShape(table_with_unbounded_vector,
                             Expected{
                                 .inline_size = 16,
                                 .alignment = 8,
                                 .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                 .depth = 3,
                                 .has_padding = true,
                                 .has_flexible_envelope = true,
                             }));

  auto table_with_unbounded_vectors = test_library.LookupTable("TableWithUnboundedVectors");
  ASSERT_NONNULL(table_with_unbounded_vectors);
  EXPECT_TRUE(CheckTypeShape(table_with_unbounded_vectors,
                             Expected{
                                 .inline_size = 16,
                                 .alignment = 8,
                                 .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                 .depth = 3,
                                 .has_padding = true,
                                 .has_flexible_envelope = true,
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
  EXPECT_TRUE(CheckTypeShape(handle_vector, Expected{
                                                .inline_size = 16,
                                                .alignment = 8,
                                                .max_out_of_line = 32,
                                                .max_handles = 8,
                                                .depth = 1,
                                                .has_padding = true,
                                            }));

  auto handle_nullable_vector = test_library.LookupStruct("HandleNullableVector");
  ASSERT_NONNULL(handle_nullable_vector);
  EXPECT_TRUE(CheckTypeShape(handle_nullable_vector, Expected{
                                                         .inline_size = 16,
                                                         .alignment = 8,
                                                         .max_out_of_line = 32,
                                                         .max_handles = 8,
                                                         .depth = 1,
                                                         .has_padding = true,
                                                     }));

  auto unbounded_handle_vector = test_library.LookupStruct("UnboundedHandleVector");
  ASSERT_NONNULL(unbounded_handle_vector);
  EXPECT_TRUE(CheckTypeShape(unbounded_handle_vector,
                             Expected{
                                 .inline_size = 16,
                                 .alignment = 8,
                                 .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                 .max_handles = std::numeric_limits<uint32_t>::max(),
                                 .depth = 1,
                                 .has_padding = true,
                             }));

  auto table_with_unbounded_handle_vector =
      test_library.LookupTable("TableWithUnboundedHandleVector");
  ASSERT_NONNULL(table_with_unbounded_handle_vector);
  EXPECT_TRUE(CheckTypeShape(table_with_unbounded_handle_vector,
                             Expected{
                                 .inline_size = 16,
                                 .alignment = 8,
                                 .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                 .max_handles = std::numeric_limits<uint32_t>::max(),
                                 .depth = 3,
                                 .has_padding = true,
                                 .has_flexible_envelope = true,
                             }));

  auto handle_struct_vector = test_library.LookupStruct("HandleStructVector");
  ASSERT_NONNULL(handle_struct_vector);
  EXPECT_TRUE(CheckTypeShape(handle_struct_vector, Expected{
                                                       .inline_size = 16,
                                                       .alignment = 8,
                                                       .max_out_of_line = 32,
                                                       .max_handles = 8,
                                                       .depth = 1,
                                                       .has_padding = true,
                                                   }));

  auto handle_table_vector = test_library.LookupStruct("HandleTableVector");
  ASSERT_NONNULL(handle_table_vector);
  EXPECT_TRUE(CheckTypeShape(handle_table_vector, Expected{
                                                      .inline_size = 16,
                                                      .alignment = 8,
                                                      .max_out_of_line = 320,
                                                      .max_handles = 8,
                                                      .depth = 3,
                                                      .has_padding = true,
                                                      .has_flexible_envelope = true,
                                                  }));

  auto table_with_handle_struct_vector = test_library.LookupTable("TableWithHandleStructVector");
  ASSERT_NONNULL(table_with_handle_struct_vector);
  EXPECT_TRUE(CheckTypeShape(table_with_handle_struct_vector, Expected{
                                                                  .inline_size = 16,
                                                                  .alignment = 8,
                                                                  .max_out_of_line = 64,
                                                                  .max_handles = 8,
                                                                  .depth = 3,
                                                                  .has_padding = true,
                                                                  .has_flexible_envelope = true,
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
  EXPECT_TRUE(CheckTypeShape(short_string, Expected{
                                               .inline_size = 16,
                                               .alignment = 8,
                                               .max_out_of_line = 8,
                                               .depth = 1,
                                               .has_padding = true,
                                           }));

  auto unbounded_string = test_library.LookupStruct("UnboundedString");
  ASSERT_NONNULL(unbounded_string);
  EXPECT_TRUE(
      CheckTypeShape(unbounded_string, Expected{
                                           .inline_size = 16,
                                           .alignment = 8,
                                           .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                           .depth = 1,
                                           .has_padding = true,
                                       }));

  auto table_with_short_string = test_library.LookupTable("TableWithShortString");
  ASSERT_NONNULL(table_with_short_string);
  EXPECT_TRUE(CheckTypeShape(table_with_short_string, Expected{
                                                          .inline_size = 16,
                                                          .alignment = 8,
                                                          .max_out_of_line = 40,
                                                          .depth = 3,
                                                          .has_padding = true,
                                                          .has_flexible_envelope = true,
                                                      }));

  auto table_with_unbounded_string = test_library.LookupTable("TableWithUnboundedString");
  ASSERT_NONNULL(table_with_unbounded_string);
  EXPECT_TRUE(CheckTypeShape(table_with_unbounded_string,
                             Expected{
                                 .inline_size = 16,
                                 .alignment = 8,
                                 .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                 .depth = 3,
                                 .has_padding = true,
                                 .has_flexible_envelope = true,
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

table TableWithAnInt32ArrayWithPadding {
  1: array<int32>:3 a;
};

table TableWithAnInt32ArrayNoPadding {
  1: array<int32>:4 a;
};

    )FIDL");
  ASSERT_TRUE(test_library.Compile());

  auto an_array = test_library.LookupStruct("AnArray");
  ASSERT_NONNULL(an_array);
  EXPECT_TRUE(CheckTypeShape(an_array, Expected{
                                           .inline_size = 40,
                                           .alignment = 8,
                                       }));

  auto table_with_an_array = test_library.LookupTable("TableWithAnArray");
  ASSERT_NONNULL(table_with_an_array);
  EXPECT_TRUE(CheckTypeShape(table_with_an_array, Expected{
                                                      .inline_size = 16,
                                                      .alignment = 8,
                                                      .max_out_of_line = 56,
                                                      .depth = 2,
                                                      .has_padding = false,
                                                      .has_flexible_envelope = true,
                                                  }));

  auto table_with_an_int32_array_with_padding =
      test_library.LookupTable("TableWithAnInt32ArrayWithPadding");
  ASSERT_NONNULL(table_with_an_int32_array_with_padding);
  EXPECT_TRUE(
      CheckTypeShape(table_with_an_int32_array_with_padding,
                     Expected{
                         .inline_size = 16,
                         .alignment = 8,
                         .max_out_of_line = 32,  // 16 table header + ALIGN(4 * 3 array) = 32
                         .depth = 2,
                         .has_padding = true,
                         .has_flexible_envelope = true,
                     }));

  auto table_with_an_int32_array_no_padding =
      test_library.LookupTable("TableWithAnInt32ArrayNoPadding");
  ASSERT_NONNULL(table_with_an_int32_array_no_padding);
  EXPECT_TRUE(
      CheckTypeShape(table_with_an_int32_array_no_padding,
                     Expected{
                         .inline_size = 16,
                         .alignment = 8,
                         .max_out_of_line = 32,  // 16 table header + ALIGN(4 * 4 array) = 32
                         .depth = 2,
                         .has_padding = false,
                         .has_flexible_envelope = true,
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
  EXPECT_TRUE(CheckTypeShape(handle_array, Expected{
                                               .inline_size = 32,
                                               .alignment = 4,
                                               .max_handles = 8,
                                           }));

  auto table_with_handle_array = test_library.LookupTable("TableWithHandleArray");
  ASSERT_NONNULL(table_with_handle_array);
  EXPECT_TRUE(CheckTypeShape(table_with_handle_array, Expected{
                                                          .inline_size = 16,
                                                          .alignment = 8,
                                                          .max_out_of_line = 48,
                                                          .max_handles = 8,
                                                          .depth = 2,
                                                          .has_padding = false,
                                                          .has_flexible_envelope = true,
                                                      }));

  auto nullable_handle_array = test_library.LookupStruct("NullableHandleArray");
  ASSERT_NONNULL(nullable_handle_array);
  EXPECT_TRUE(CheckTypeShape(nullable_handle_array, Expected{
                                                        .inline_size = 32,
                                                        .alignment = 4,
                                                        .max_handles = 8,
                                                    }));

  auto table_with_nullable_handle_array = test_library.LookupTable("TableWithNullableHandleArray");
  ASSERT_NONNULL(table_with_nullable_handle_array);
  EXPECT_TRUE(CheckTypeShape(table_with_nullable_handle_array, Expected{
                                                                   .inline_size = 16,
                                                                   .alignment = 8,
                                                                   .max_out_of_line = 48,
                                                                   .max_handles = 8,
                                                                   .depth = 2,
                                                                   .has_padding = false,
                                                                   .has_flexible_envelope = true,
                                                               }));

  END_TEST;
}

static bool xunions() {
  BEGIN_TEST;

  TestLibrary test_library(R"FIDL(
library example;

xunion XUnionWithOneBool {
  1: bool b;
};

struct StructWithOptionalXUnionWithOneBool {
  XUnionWithOneBool? opt_xunion_with_bool;
};

xunion XUnionWithBoundedOutOfLineObject {
  // smaller than |v| below, so will not be selected for max-out-of-line
  // calculation.
  1: bool b;

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
  2: vector<vector<int32>:5>:6 v;
};

xunion XUnionWithUnboundedOutOfLineObject {
  1: string s;
};

xunion XUnionWithoutPayloadPadding {
  1: array<uint64>:7 a;
};

xunion PaddingCheck {
  1: array<uint8>:3 three;
  2: array<uint8>:5 five;
};
    )FIDL");
  ASSERT_TRUE(test_library.Compile());

  auto one_bool = test_library.LookupXUnion("XUnionWithOneBool");
  ASSERT_NONNULL(one_bool);
  EXPECT_TRUE(CheckTypeShape(one_bool, Expected{
                                           .inline_size = 24,
                                           .alignment = 8,
                                           .max_out_of_line = 8,
                                           .depth = 1,
                                           .has_padding = true,
                                           .has_flexible_envelope = true,
                                       }));
  ASSERT_EQ(one_bool->members.size(), 1);
  ASSERT_NONNULL(one_bool->members[0].maybe_used);
  EXPECT_TRUE(CheckFieldShape(*one_bool->members[0].maybe_used, ExpectedField{.padding = 7}));

  auto opt_one_bool = test_library.LookupStruct("StructWithOptionalXUnionWithOneBool");
  ASSERT_NONNULL(opt_one_bool);
  EXPECT_TRUE(CheckTypeShape(opt_one_bool, Expected{
                                               .inline_size = 24,
                                               .alignment = 8,
                                               .max_out_of_line = 8,
                                               .depth = 1,
                                               .has_padding = true,
                                               .has_flexible_envelope = true,
                                           }));

  auto xu = test_library.LookupXUnion("XUnionWithBoundedOutOfLineObject");
  ASSERT_NONNULL(xu);
  EXPECT_TRUE(CheckTypeShape(xu, Expected{
                                     .inline_size = 24,
                                     .alignment = 8,
                                     .max_out_of_line = 256,
                                     .depth = 3,
                                     .has_padding = true,
                                     .has_flexible_envelope = true,
                                 }));

  auto unbounded = test_library.LookupXUnion("XUnionWithUnboundedOutOfLineObject");
  ASSERT_NONNULL(unbounded);
  EXPECT_TRUE(CheckTypeShape(unbounded, Expected{
                                            .inline_size = 24,
                                            .alignment = 8,
                                            .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                            .depth = 2,
                                            .has_padding = true,
                                            .has_flexible_envelope = true,
                                        }));

  auto xu_no_payload_padding = test_library.LookupXUnion("XUnionWithoutPayloadPadding");
  ASSERT_NONNULL(xu_no_payload_padding);
  EXPECT_TRUE(CheckTypeShape(xu_no_payload_padding,
                             Expected{
                                 .inline_size = 24,
                                 .alignment = 8,
                                 .max_out_of_line = 56,
                                 .depth = 1,
                                 // xunion always have padding, because its ordinal is 32 bits.
                                 // TODO(FIDL-648): increase the ordinal size to 64 bits, such that
                                 // there is no padding.
                                 .has_padding = true,
                                 .has_flexible_envelope = true,
                             }));

  auto padding_check = test_library.LookupXUnion("PaddingCheck");
  ASSERT_NONNULL(padding_check);
  EXPECT_TRUE(CheckTypeShape(padding_check, Expected{
                                                .inline_size = 24,
                                                .alignment = 8,
                                                .max_out_of_line = 8,
                                                .depth = 1,
                                                .has_padding = true,
                                                .has_flexible_envelope = true,
                                            }));
  ASSERT_EQ(padding_check->members.size(), 2);
  ASSERT_NONNULL(padding_check->members[0].maybe_used);
  EXPECT_TRUE(CheckFieldShape(*padding_check->members[0].maybe_used, ExpectedField{.padding = 5}));
  EXPECT_TRUE(CheckFieldShape(*padding_check->members[1].maybe_used, ExpectedField{.padding = 3}));

  END_TEST;
}

bool envelope_strictness() {
  BEGIN_TEST;

  TestLibrary test_library(R"FIDL(
library example;

strict xunion StrictLeafXUnion {
    1: int64 a;
};

xunion FlexibleLeafXUnion {
    1: int64 a;
};

xunion FlexibleXUnionOfStrictXUnion {
    1: StrictLeafXUnion xu;
};

xunion FlexibleXUnionOfFlexibleXUnion {
    1: FlexibleLeafXUnion xu;
};

strict xunion StrictXUnionOfStrictXUnion {
    1: StrictLeafXUnion xu;
};

strict xunion StrictXUnionOfFlexibleXUnion {
    1: FlexibleLeafXUnion xu;
};

table FlexibleLeafTable {
};

strict xunion StrictXUnionOfFlexibleTable {
    1: FlexibleLeafTable ft;
};

    )FIDL");
  ASSERT_TRUE(test_library.Compile());

  auto strict_xunion = test_library.LookupXUnion("StrictLeafXUnion");
  ASSERT_NONNULL(strict_xunion);
  EXPECT_TRUE(CheckTypeShape(strict_xunion, Expected{
                                                .inline_size = 24,
                                                .alignment = 8,
                                                .max_out_of_line = 8,
                                                .depth = 1,
                                                .has_padding = true,
                                            }));

  auto flexible_xunion = test_library.LookupXUnion("FlexibleLeafXUnion");
  ASSERT_NONNULL(flexible_xunion);
  EXPECT_TRUE(CheckTypeShape(flexible_xunion, Expected{
                                                  .inline_size = 24,
                                                  .alignment = 8,
                                                  .max_out_of_line = 8,
                                                  .depth = 1,
                                                  .has_padding = true,
                                                  .has_flexible_envelope = true,
                                              }));

  auto flexible_of_strict = test_library.LookupXUnion("FlexibleXUnionOfStrictXUnion");
  ASSERT_NONNULL(flexible_of_strict);
  EXPECT_TRUE(CheckTypeShape(flexible_of_strict, Expected{
                                                     .inline_size = 24,
                                                     .alignment = 8,
                                                     .max_out_of_line = 32,
                                                     .depth = 2,
                                                     .has_padding = true,
                                                     .has_flexible_envelope = true,
                                                 }));

  auto flexible_of_flexible = test_library.LookupXUnion("FlexibleXUnionOfFlexibleXUnion");
  ASSERT_NONNULL(flexible_of_flexible);
  EXPECT_TRUE(CheckTypeShape(flexible_of_flexible, Expected{
                                                       .inline_size = 24,
                                                       .alignment = 8,
                                                       .max_out_of_line = 32,
                                                       .depth = 2,
                                                       .has_padding = true,
                                                       .has_flexible_envelope = true,
                                                   }));

  auto strict_of_strict = test_library.LookupXUnion("StrictXUnionOfStrictXUnion");
  ASSERT_NONNULL(strict_of_strict);
  EXPECT_TRUE(CheckTypeShape(strict_of_strict, Expected{
                                                   .inline_size = 24,
                                                   .alignment = 8,
                                                   .max_out_of_line = 32,
                                                   .depth = 2,
                                                   .has_padding = true,
                                                   .has_flexible_envelope = false,
                                               }));

  auto strict_of_flexible = test_library.LookupXUnion("StrictXUnionOfFlexibleXUnion");
  ASSERT_NONNULL(strict_of_flexible);
  EXPECT_TRUE(CheckTypeShape(strict_of_flexible, Expected{
                                                     .inline_size = 24,
                                                     .alignment = 8,
                                                     .max_out_of_line = 32,
                                                     .depth = 2,
                                                     .has_padding = true,
                                                     .has_flexible_envelope = true,
                                                 }));

  auto flexible_table = test_library.LookupTable("FlexibleLeafTable");
  ASSERT_NONNULL(flexible_table);
  EXPECT_TRUE(CheckTypeShape(flexible_table, Expected{
                                                 .inline_size = 16,
                                                 .alignment = 8,
                                                 .max_out_of_line = 0,
                                                 .depth = 1,
                                                 .has_padding = false,
                                                 .has_flexible_envelope = true,
                                             }));

  auto strict_xunion_of_flexible_table = test_library.LookupXUnion("StrictXUnionOfFlexibleTable");
  ASSERT_NONNULL(strict_xunion_of_flexible_table);
  EXPECT_TRUE(CheckTypeShape(strict_xunion_of_flexible_table, Expected{
                                                                  .inline_size = 24,
                                                                  .alignment = 8,
                                                                  .max_out_of_line = 16,
                                                                  .depth = 2,
                                                                  .has_padding = true,
                                                                  .has_flexible_envelope = true,
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
  EXPECT_TRUE(CheckTypeShape(using_some_protocol, Expected{
                                                      .inline_size = 4,
                                                      .alignment = 4,
                                                      .max_handles = 1,
                                                  }));

  auto using_opt_some_protocol = test_library.LookupStruct("UsingOptSomeProtocol");
  ASSERT_NONNULL(using_opt_some_protocol);
  EXPECT_TRUE(CheckTypeShape(using_opt_some_protocol, Expected{
                                                          .inline_size = 4,
                                                          .alignment = 4,
                                                          .max_handles = 1,
                                                      }));

  auto using_request_some_protocol = test_library.LookupStruct("UsingRequestSomeProtocol");
  ASSERT_NONNULL(using_request_some_protocol);
  EXPECT_TRUE(CheckTypeShape(using_request_some_protocol, Expected{
                                                              .inline_size = 4,
                                                              .alignment = 4,
                                                              .max_handles = 1,
                                                          }));

  auto using_opt_request_some_protocol = test_library.LookupStruct("UsingOptRequestSomeProtocol");
  ASSERT_NONNULL(using_opt_request_some_protocol);
  EXPECT_TRUE(CheckTypeShape(using_opt_request_some_protocol, Expected{
                                                                  .inline_size = 4,
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
  EXPECT_TRUE(CheckTypeShape(ext_struct, Expected{
                                             .inline_size = 4,
                                             .alignment = 4,
                                         }));

  auto ext_arr_struct = test_library.LookupStruct("ExternalArrayStruct");
  ASSERT_NONNULL(ext_arr_struct);
  EXPECT_TRUE(CheckTypeShape(ext_arr_struct, Expected{
                                                 .inline_size = 4 * 32,
                                                 .alignment = 4,
                                             }));

  auto ext_str_struct = test_library.LookupStruct("ExternalStringSizeStruct");
  ASSERT_NONNULL(ext_str_struct);
  EXPECT_TRUE(CheckTypeShape(ext_str_struct, Expected{
                                                 .inline_size = 16,
                                                 .alignment = 8,
                                                 .max_out_of_line = 32,
                                                 .depth = 1,
                                                 .has_padding = true,
                                             }));

  auto ext_vec_struct = test_library.LookupStruct("ExternalVectorSizeStruct");
  ASSERT_NONNULL(ext_vec_struct);
  EXPECT_TRUE(CheckTypeShape(ext_vec_struct, Expected{
                                                 .inline_size = 16,
                                                 .alignment = 8,
                                                 .max_out_of_line = 32 * 4,
                                                 .max_handles = 32,
                                                 .depth = 1,
                                                 .has_padding = true,
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
  EXPECT_TRUE(CheckTypeShape(web_message, Expected{
                                              .inline_size = 4,
                                              .alignment = 4,
                                              .max_handles = 1,
                                          }));
  ASSERT_EQ(web_message->members.size(), 1);
  EXPECT_TRUE(CheckFieldShape(web_message->members[0], ExpectedField{}));

  auto message_port = library.LookupProtocol("MessagePort");
  ASSERT_NONNULL(message_port);
  ASSERT_EQ(message_port->methods.size(), 1);
  auto& post_message = message_port->methods[0];
  auto post_message_request = post_message.maybe_request;
  ASSERT_NONNULL(post_message_request);
  EXPECT_TRUE(CheckTypeShape(post_message_request, Expected{
                                                       .inline_size = 24,
                                                       .alignment = 8,
                                                       .max_handles = 1,
                                                       .has_padding = true,
                                                   }));
  ASSERT_EQ(post_message_request->members.size(), 1);
  EXPECT_TRUE(
      CheckFieldShape(post_message_request->members[0], ExpectedField{.offset = 16, .padding = 4}));

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
  EXPECT_TRUE(CheckTypeShape(web_message, Expected{
                                              .inline_size = 4,
                                              .alignment = 4,
                                              .max_handles = 1,
                                          }));

  auto message_port = library.LookupProtocol("MessagePort");
  ASSERT_NONNULL(message_port);
  ASSERT_EQ(message_port->methods.size(), 1);
  auto& post_message = message_port->methods[0];
  auto post_message_request = post_message.maybe_request;
  ASSERT_NONNULL(post_message_request);
  EXPECT_TRUE(CheckTypeShape(post_message_request, Expected{
                                                       .inline_size = 24,
                                                       .alignment = 8,
                                                       .max_handles = 1,
                                                       .has_padding = true,
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
  EXPECT_TRUE(CheckTypeShape(web_message, Expected{
                                              .inline_size = 4,
                                              .alignment = 4,
                                              .max_handles = 1,
                                          }));

  auto message_port = library.LookupProtocol("MessagePort");
  ASSERT_NONNULL(message_port);
  ASSERT_EQ(message_port->methods.size(), 1);
  auto& post_message = message_port->methods[0];
  auto post_message_request = post_message.maybe_request;
  ASSERT_NONNULL(post_message_request);
  EXPECT_TRUE(CheckTypeShape(post_message_request, Expected{
                                                       .inline_size = 24,
                                                       .alignment = 8,
                                                       .max_handles = 1,
                                                       .has_padding = true,
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
  EXPECT_TRUE(CheckTypeShape(web_message, Expected{
                                              .inline_size = 4,
                                              .alignment = 4,
                                              .max_handles = 1,
                                          }));

  auto message_port = library.LookupProtocol("MessagePort");
  ASSERT_NONNULL(message_port);
  ASSERT_EQ(message_port->methods.size(), 1);
  auto& post_message = message_port->methods[0];
  auto post_message_request = post_message.maybe_request;
  ASSERT_NONNULL(post_message_request);
  EXPECT_TRUE(CheckTypeShape(post_message_request, Expected{
                                                       .inline_size = 24,
                                                       .alignment = 8,
                                                       .max_handles = 1,
                                                       .has_padding = true,
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
  EXPECT_TRUE(
      CheckTypeShape(the_struct, Expected{
                                     .inline_size = 8,
                                     .alignment = 8,
                                     .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                     .max_handles = 0,
                                     .depth = std::numeric_limits<uint32_t>::max(),
                                 }));
  ASSERT_EQ(the_struct->members.size(), 1);
  EXPECT_TRUE(CheckFieldShape(the_struct->members[0], ExpectedField{}));

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
  EXPECT_TRUE(
      CheckTypeShape(the_struct, Expected{.inline_size = 16,
                                          .alignment = 8,
                                          .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                          .max_handles = std::numeric_limits<uint32_t>::max(),
                                          .depth = std::numeric_limits<uint32_t>::max(),
                                          .has_padding = true}));
  ASSERT_EQ(the_struct->members.size(), 2);
  EXPECT_TRUE(CheckFieldShape(the_struct->members[0], ExpectedField{
                                                          .padding = 4,
                                                      }));
  EXPECT_TRUE(CheckFieldShape(the_struct->members[1], ExpectedField{
                                                          .offset = 8,
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
  EXPECT_TRUE(CheckTypeShape(struct_a, Expected{
                                           .inline_size = 8,
                                           .alignment = 8,
                                           .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                           .max_handles = 0,
                                           .depth = std::numeric_limits<uint32_t>::max(),
                                       }));

  auto struct_b = library.LookupStruct("B");
  ASSERT_NONNULL(struct_b);
  EXPECT_TRUE(CheckTypeShape(struct_b, Expected{
                                           .inline_size = 8,
                                           .alignment = 8,
                                           .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                           .max_handles = 0,
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
  EXPECT_TRUE(CheckTypeShape(struct_a, Expected{
                                           .inline_size = 16,
                                           .alignment = 8,
                                           .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                           .max_handles = std::numeric_limits<uint32_t>::max(),
                                           .depth = std::numeric_limits<uint32_t>::max(),
                                           .has_padding = true,
                                       }));

  auto struct_b = library.LookupStruct("B");
  ASSERT_NONNULL(struct_b);
  EXPECT_TRUE(CheckTypeShape(struct_b, Expected{
                                           .inline_size = 16,
                                           .alignment = 8,
                                           .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                           .max_handles = std::numeric_limits<uint32_t>::max(),
                                           .depth = std::numeric_limits<uint32_t>::max(),
                                           .has_padding = true,
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
  EXPECT_TRUE(
      CheckTypeShape(struct_foo, Expected{
                                     .inline_size = 8,
                                     .alignment = 8,
                                     .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                     .max_handles = 0,
                                     .depth = std::numeric_limits<uint32_t>::max(),
                                 }));

  auto struct_bar = library.LookupStruct("Bar");
  ASSERT_NONNULL(struct_bar);
  EXPECT_TRUE(
      CheckTypeShape(struct_bar, Expected{
                                     .inline_size = 8,
                                     .alignment = 8,
                                     .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                     .max_handles = 0,
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
  EXPECT_TRUE(CheckTypeShape(buffer, Expected{
                                         .inline_size = 16,
                                         .alignment = 8,
                                         .max_handles = 1,
                                         .has_padding = true,
                                     }));

  auto value = library.LookupStruct("Value");
  ASSERT_NONNULL(value);
  EXPECT_TRUE(CheckTypeShape(
      value, Expected{
                 .inline_size = 16,
                 .alignment = 8,
                 .max_out_of_line = 16,
                 .max_handles = 1,
                 .depth = 1,
                 .has_padding = true,  // because the size of |Priority| defaults to uint32
             }));

  auto diff_entry = library.LookupStruct("DiffEntry");
  ASSERT_NONNULL(diff_entry);
  EXPECT_TRUE(CheckTypeShape(diff_entry, Expected{
                                             .inline_size = 40,
                                             .alignment = 8,
                                             .max_out_of_line = 352,
                                             .max_handles = 3,
                                             .depth = 2,
                                             .has_padding = true  // because |Value| has padding
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
)FIDL",
                             &shared);
  ASSERT_TRUE(parent_library.Compile());

  TestLibrary child_library("child.fidl", R"FIDL(
library child;

using parent;

protocol Child {
  compose parent.Parent;
};
)FIDL",
                            &shared);
  ASSERT_TRUE(child_library.AddDependentLibrary(std::move(parent_library)));
  ASSERT_TRUE(child_library.Compile());

  auto child = child_library.LookupProtocol("Child");
  ASSERT_NONNULL(child);
  ASSERT_EQ(child->all_methods.size(), 1);
  auto& sync_with_info = child->all_methods[0];
  auto sync_request = sync_with_info.method->maybe_request;
  ASSERT_NONNULL(sync_request);
  EXPECT_TRUE(CheckTypeShape(sync_request, Expected{
                                               .inline_size = 16,
                                               .alignment = 8,
                                           }));

  END_TEST;
}

bool union_size8alignment4_sandwich() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

union UnionSize8Alignment4 {
    1: uint32 variant;
};

struct Sandwich {
    uint32 before;
    UnionSize8Alignment4 union;
    uint32 after;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto sandwich = library.LookupStruct("Sandwich");
  ASSERT_NONNULL(sandwich);
  EXPECT_TRUE(CheckTypeShape(sandwich,
                             Expected{
                                 .inline_size = 16,
                                 .alignment = 4,
                                 .max_handles = 0,
                                 .has_padding = false,
                                 .contains_union = true,
                             },
                             Expected{
                                 .inline_size = 40,
                                 .alignment = 8,
                                 .max_out_of_line = 8,
                                 .max_handles = 0,
                                 .depth = 1,
                                 .has_padding = true,
                                 .contains_union = true,
                             }));
  ASSERT_EQ(sandwich->members.size(), 3);
  EXPECT_TRUE(CheckFieldShape(sandwich->members[0],  // before
                              ExpectedField{
                                  .offset = 0,
                                  .padding = 0,
                              },
                              ExpectedField{
                                  .offset = 0,
                                  .padding = 4,
                              }));
  EXPECT_TRUE(CheckFieldShape(sandwich->members[1],  // union
                              ExpectedField{
                                  .offset = 4,
                                  .padding = 0,
                              },
                              ExpectedField{
                                  .offset = 8,
                                  .padding = 0,
                              }));
  EXPECT_TRUE(CheckFieldShape(sandwich->members[2],  // after
                              ExpectedField{
                                  .offset = 12,
                                  .padding = 0,
                              },
                              ExpectedField{
                                  .offset = 32,
                                  .padding = 4,
                              }));

  END_TEST;
}

bool union_size12alignment4_sandwich() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

union UnionSize12Alignment4 {
    1: array<uint8>:6 variant;
};

struct Sandwich {
    uint32 before;
    UnionSize12Alignment4 union;
    int32 after;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto sandwich = library.LookupStruct("Sandwich");
  ASSERT_NONNULL(sandwich);
  EXPECT_TRUE(CheckTypeShape(sandwich,
                             Expected{
                                 .inline_size = 20,
                                 .alignment = 4,
                                 .max_handles = 0,
                                 .has_padding = true,
                                 .contains_union = true,
                             },
                             Expected{
                                 .inline_size = 40,
                                 .alignment = 8,
                                 .max_out_of_line = 8,
                                 .max_handles = 0,
                                 .depth = 1,
                                 .has_padding = true,
                                 .contains_union = true,
                             }));
  ASSERT_EQ(sandwich->members.size(), 3);
  EXPECT_TRUE(CheckFieldShape(sandwich->members[0],  // before
                              ExpectedField{
                                  .offset = 0,
                                  .padding = 0,
                              },
                              ExpectedField{
                                  .offset = 0,
                                  .padding = 4,
                              }));
  EXPECT_TRUE(CheckFieldShape(sandwich->members[1],  // union
                              ExpectedField{
                                  .offset = 4,
                                  .padding = 0,
                              },
                              ExpectedField{
                                  .offset = 8,
                                  .padding = 0,
                              }));
  EXPECT_TRUE(CheckFieldShape(sandwich->members[2],  // after
                              ExpectedField{
                                  .offset = 16,
                                  .padding = 0,
                              },
                              ExpectedField{
                                  .offset = 32,
                                  .padding = 4,
                              }));

  END_TEST;
}

bool union_size24alignment8_sandwich() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct StructSize16Alignment8 {
    uint64 f1;
    uint64 f2;
};

union UnionSize24Alignment8 {
    1: StructSize16Alignment8 variant;
};

struct Sandwich {
    uint32 before;
    UnionSize24Alignment8 union;
    uint32 after;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto sandwich = library.LookupStruct("Sandwich");
  ASSERT_NONNULL(sandwich);
  EXPECT_TRUE(CheckTypeShape(sandwich,
                             Expected{
                                 .inline_size = 40,
                                 .alignment = 8,
                                 .max_handles = 0,
                                 .has_padding = true,
                                 .contains_union = true,
                             },
                             Expected{
                                 .inline_size = 40,
                                 .alignment = 8,
                                 .max_out_of_line = 16,
                                 .max_handles = 0,
                                 .depth = 1,
                                 .has_padding = true,
                                 .contains_union = true,
                             }));
  ASSERT_EQ(sandwich->members.size(), 3);
  EXPECT_TRUE(CheckFieldShape(sandwich->members[0],  // before
                              ExpectedField{
                                  .offset = 0,
                                  .padding = 4,
                              },
                              ExpectedField{
                                  .offset = 0,
                                  .padding = 4,
                              }));
  EXPECT_TRUE(CheckFieldShape(sandwich->members[1],  // union
                              ExpectedField{
                                  .offset = 8,
                                  .padding = 0,
                              },
                              ExpectedField{
                                  .offset = 8,
                                  .padding = 0,
                              }));
  EXPECT_TRUE(CheckFieldShape(sandwich->members[2],  // after
                              ExpectedField{
                                  .offset = 32,
                                  .padding = 4,
                              },
                              ExpectedField{
                                  .offset = 32,
                                  .padding = 4,
                              }));

  END_TEST;
}

bool union_size36alignment4_sandwich() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

union UnionSize36Alignment4 {
    1: array<uint8>:32 variant;
};

struct Sandwich {
    uint32 before;
    UnionSize36Alignment4 union;
    uint32 after;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto sandwich = library.LookupStruct("Sandwich");
  ASSERT_NONNULL(sandwich);
  EXPECT_TRUE(CheckTypeShape(sandwich,
                             Expected{
                                 .inline_size = 44,
                                 .alignment = 4,
                                 .max_handles = 0,
                                 .has_padding = false,
                                 .contains_union = true,
                             },
                             Expected{
                                 .inline_size = 40,
                                 .alignment = 8,
                                 .max_out_of_line = 32,
                                 .max_handles = 0,
                                 .depth = 1,
                                 .has_padding = true,
                                 .contains_union = true,
                             }));
  ASSERT_EQ(sandwich->members.size(), 3);
  EXPECT_TRUE(CheckFieldShape(sandwich->members[0],  // before
                              ExpectedField{
                                  .offset = 0,
                                  .padding = 0,
                              },
                              ExpectedField{
                                  .offset = 0,
                                  .padding = 4,
                              }));
  EXPECT_TRUE(CheckFieldShape(sandwich->members[1],  // union
                              ExpectedField{
                                  .offset = 4,
                                  .padding = 0,
                              },
                              ExpectedField{
                                  .offset = 8,
                                  .padding = 0,
                              }));
  EXPECT_TRUE(CheckFieldShape(sandwich->members[2],  // after
                              ExpectedField{
                                  .offset = 40,
                                  .padding = 0,
                              },
                              ExpectedField{
                                  .offset = 32,
                                  .padding = 4,
                              }));

  END_TEST;
}

bool no_transitive_unions() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

union NotUsed {
  1: int32 foo;
};

struct ChildStruct {
  int32 bar;
};

struct MiddleStruct {
  ChildStruct child;
  array<uint8>:32 foo;
};

struct RootStruct {
  MiddleStruct child;
  ChildStruct leaf;
  vector<int8>:10 foo;
};

table SomeTable {
  1: RootStruct child;
};

enum SomeEnum : uint32 {
  FOO = 1;
  BAR = 2;
};

bits SomeBits : uint64 {
  kOne = 1;
  kTwo = 2;
};

)FIDL");
  ASSERT_TRUE(library.Compile());
  auto child_struct = library.LookupStruct("ChildStruct");
  ASSERT_NONNULL(child_struct);
  EXPECT_TRUE(CheckContainsUnion(child_struct, false));

  auto middle_struct = library.LookupStruct("MiddleStruct");
  ASSERT_NONNULL(middle_struct);
  EXPECT_TRUE(CheckContainsUnion(middle_struct, false));

  auto root_struct = library.LookupStruct("RootStruct");
  ASSERT_NONNULL(root_struct);
  EXPECT_TRUE(CheckContainsUnion(root_struct, false));

  auto some_table = library.LookupTable("SomeTable");
  ASSERT_NONNULL(some_table);
  EXPECT_TRUE(CheckContainsUnion(some_table, false));

  auto some_enum = library.LookupEnum("SomeEnum");
  ASSERT_NONNULL(some_enum);
  EXPECT_TRUE(CheckContainsUnion(some_enum, false));

  auto some_bits = library.LookupBits("SomeBits");
  ASSERT_NONNULL(some_bits);
  EXPECT_TRUE(CheckContainsUnion(some_bits, false));
  END_TEST;
}

bool transitive_union_result_type() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library test;

protocol Foo {
  WithError(int8 x, int8 y) -> (int32 out) error int32;
};

)FIDL");
  ASSERT_TRUE(library.Compile());
  auto result_type = library.LookupUnion("Foo_WithError_Result");
  ASSERT_NONNULL(result_type);
  EXPECT_TRUE(CheckContainsUnion(result_type, true));

  END_TEST;
}

bool transitive_union_nested() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library test;

union DeepUnion {
  1: int32 foo;
};

struct Level1 {
  DeepUnion child;
};

struct Level2 {
  Level1 child;
};

table Mixed {
  1: DeepUnion foo;
  2: Level2 bar;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto inner_union = library.LookupUnion("DeepUnion");
  ASSERT_NONNULL(inner_union);
  EXPECT_TRUE(CheckContainsUnion(inner_union, true));

  auto level1 = library.LookupStruct("Level1");
  ASSERT_NONNULL(level1);
  EXPECT_TRUE(CheckContainsUnion(level1, true));

  auto level2 = library.LookupStruct("Level2");
  ASSERT_NONNULL(level2);
  EXPECT_TRUE(CheckContainsUnion(level2, true));

  auto mixed_table = library.LookupTable("Mixed");
  ASSERT_NONNULL(mixed_table);
  EXPECT_TRUE(CheckContainsUnion(mixed_table, true));

  END_TEST;
}

bool transitive_union_layered() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library test;

enum DeepestEnum {
  FOO = 1;
  BAR = 2;
};

table InsideUnion {
  1: DeepestEnum child;
};

union InnerUnion {
  1: int32 foo;
  2: InsideUnion bar;
};

struct ContainsUnion {
  InnerUnion foo;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto deepest_enum = library.LookupEnum("DeepestEnum");
  ASSERT_NONNULL(deepest_enum);
  EXPECT_TRUE(CheckContainsUnion(deepest_enum, false));

  auto inside_union = library.LookupTable("InsideUnion");
  ASSERT_NONNULL(inside_union);
  EXPECT_TRUE(CheckContainsUnion(inside_union, false));

  auto inner_union = library.LookupUnion("InnerUnion");
  ASSERT_NONNULL(inner_union);
  EXPECT_TRUE(CheckContainsUnion(inner_union, true));

  auto contains_union = library.LookupStruct("ContainsUnion");
  ASSERT_NONNULL(contains_union);
  EXPECT_TRUE(CheckContainsUnion(contains_union, true));

  END_TEST;
}

bool transitive_union_xunion() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library test;

xunion InnerXUnion {
  1: int32 foo;
};

union MiddleUnion {
  1: int32 foo;
  2: InnerXUnion bar;
};

xunion OuterXUnion {
  1: MiddleUnion foo;
};

)FIDL");
  ASSERT_TRUE(library.Compile());

  auto inner_xunion = library.LookupXUnion("InnerXUnion");
  ASSERT_NONNULL(inner_xunion);
  EXPECT_TRUE(CheckContainsUnion(inner_xunion, false));

  auto middle_union = library.LookupUnion("MiddleUnion");
  ASSERT_NONNULL(middle_union);
  EXPECT_TRUE(CheckContainsUnion(middle_union, true));

  auto outer_xunion = library.LookupXUnion("OuterXUnion");
  ASSERT_NONNULL(outer_xunion);
  EXPECT_TRUE(CheckContainsUnion(outer_xunion, true));

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(typeshape_tests)
RUN_TEST(empty_struct)
RUN_TEST(empty_struct_within_another_struct)
RUN_TEST(simple_structs)
RUN_TEST(simple_structs_with_handles)
RUN_TEST(bits)
RUN_TEST(simple_tables)
RUN_TEST(tables_with_reserved_fields)
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
// RUN_TEST(xunions_with_handles) TODO(pascallouis): write it.
RUN_TEST(envelope_strictness)
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
RUN_TEST(union_size8alignment4_sandwich)
RUN_TEST(union_size12alignment4_sandwich)
RUN_TEST(union_size24alignment8_sandwich)
RUN_TEST(union_size36alignment4_sandwich)
RUN_TEST(no_transitive_unions)
RUN_TEST(transitive_union_result_type)
RUN_TEST(transitive_union_nested)
RUN_TEST(transitive_union_layered)
RUN_TEST(transitive_union_xunion)
END_TEST_CASE(typeshape_tests)
