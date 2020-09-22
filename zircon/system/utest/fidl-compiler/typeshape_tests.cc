// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>
#include <fidl/type_shape.h>
#include <zxtest/zxtest.h>

#include "test_library.h"

namespace {

const std::string kPrologWithHandleDefinition(R"FIDL(
library example;

enum obj_type : uint32 {
    NONE = 0;
    PROCESS = 1;
    THREAD = 2;
    VMO = 3;
    CHANNEL = 4;
    PORT = 6;
    TIMER = 22;
};

resource_definition handle : uint32 {
    properties {
        obj_type subtype;
    };
};
)FIDL");

struct Expected {
  uint32_t inline_size = 0;
  uint32_t alignment = 0;
  uint32_t max_out_of_line = 0;
  uint32_t max_handles = 0;
  uint32_t depth = 0;
  bool has_padding = false;
  bool has_flexible_envelope = false;
  bool is_resource = false;
};

void CheckTypeShape(const fidl::TypeShape& actual, Expected expected) {
  EXPECT_EQ(actual.InlineSize(), expected.inline_size);
  EXPECT_EQ(actual.Alignment(), expected.alignment);
  EXPECT_EQ(actual.MaxOutOfLine(), expected.max_out_of_line);
  EXPECT_EQ(actual.MaxHandles(), expected.max_handles);
  EXPECT_EQ(actual.Depth(), expected.depth);
  EXPECT_EQ(actual.HasPadding(), expected.has_padding);
  EXPECT_EQ(actual.HasFlexibleEnvelope(), expected.has_flexible_envelope);
  EXPECT_EQ(actual.is_resource, expected.is_resource);
}

void CheckTypeShape(const fidl::flat::Object* actual, Expected expected_old,
                    Expected expected_v1_no_ee, Expected expected_v1_header) {
  ASSERT_NO_FAILURES(
      CheckTypeShape(fidl::TypeShape(actual, fidl::WireFormat::kV1NoEe), expected_v1_no_ee));
  ASSERT_NO_FAILURES(
      CheckTypeShape(fidl::TypeShape(actual, fidl::WireFormat::kV1Header), expected_v1_header));
}

void CheckTypeShape(const fidl::flat::Object* actual, Expected expected_old,
                    Expected expected_v1_no_ee) {
  ASSERT_NO_FAILURES(
      CheckTypeShape(fidl::TypeShape(actual, fidl::WireFormat::kV1NoEe), expected_v1_no_ee));
  ASSERT_NO_FAILURES(
      CheckTypeShape(fidl::TypeShape(actual, fidl::WireFormat::kV1Header), expected_v1_no_ee));
}

void CheckTypeShape(const fidl::flat::Object* actual, Expected expected) {
  CheckTypeShape(actual, expected, expected);
}

struct ExpectedField {
  uint32_t offset = 0;
  uint32_t padding = 0;
};

template <typename T>
void CheckFieldShape(const T& field, ExpectedField expected_old, ExpectedField expected_v1) {
  // There is no difference between kV1NoEe and kV1Header when it comes to FieldShapes, so
  // only test one of them.
  const fidl::FieldShape& actual_v1 = fidl::FieldShape(field, fidl::WireFormat::kV1NoEe);
  EXPECT_EQ(actual_v1.offset, expected_v1.offset);
  EXPECT_EQ(actual_v1.padding, expected_v1.padding);
}

template <typename T>
void CheckFieldShape(const T& field, ExpectedField expected_old) {
  CheckFieldShape(field, expected_old, expected_old);
}

TEST(TypeshapeTests, empty_struct) {
  TestLibrary test_library(R"FIDL(
library example;

struct Empty {};

    )FIDL");
  ASSERT_TRUE(test_library.Compile());

  auto empty = test_library.LookupStruct("Empty");
  ASSERT_NOT_NULL(empty);
  ASSERT_NO_FAILURES(CheckTypeShape(empty, Expected{
                                               .inline_size = 1,
                                               .alignment = 1,
                                           }));
  ASSERT_EQ(empty->members.size(), 0);
}

TEST(TypeshapeTests, empty_struct_within_another_struct) {
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
  ASSERT_NOT_NULL(empty_with_other_things);
  ASSERT_NO_FAILURES(CheckTypeShape(empty_with_other_things, Expected{
                                                                 .inline_size = 16,
                                                                 .alignment = 4,
                                                                 .has_padding = true,
                                                             }));
  ASSERT_EQ(empty_with_other_things->members.size(), 8);
  // bool a;
  ASSERT_NO_FAILURES(CheckFieldShape(empty_with_other_things->members[0], ExpectedField{}));
  // Empty b;
  ASSERT_NO_FAILURES(CheckFieldShape(empty_with_other_things->members[1], ExpectedField{
                                                                              .offset = 1,
                                                                          }));
  // int16 c;
  ASSERT_NO_FAILURES(CheckFieldShape(empty_with_other_things->members[2], ExpectedField{
                                                                              .offset = 2,
                                                                          }));
  // Empty d;
  ASSERT_NO_FAILURES(CheckFieldShape(empty_with_other_things->members[3],
                                     ExpectedField{.offset = 4, .padding = 3}));
  // int32 e;
  ASSERT_NO_FAILURES(CheckFieldShape(empty_with_other_things->members[4], ExpectedField{
                                                                              .offset = 8,
                                                                          }));
  // int16 f;
  ASSERT_NO_FAILURES(CheckFieldShape(empty_with_other_things->members[5], ExpectedField{
                                                                              .offset = 12,
                                                                          }));
  // Empty g;
  ASSERT_NO_FAILURES(CheckFieldShape(empty_with_other_things->members[6], ExpectedField{
                                                                              .offset = 14,
                                                                          }));
  // Empty h;
  ASSERT_NO_FAILURES(CheckFieldShape(empty_with_other_things->members[7], ExpectedField{
                                                                              .offset = 15,
                                                                          }));
}

TEST(TypeshapeTests, simple_structs) {
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
  ASSERT_NOT_NULL(one_bool);
  ASSERT_NO_FAILURES(CheckTypeShape(one_bool, Expected{
                                                  .inline_size = 1,
                                                  .alignment = 1,
                                              }));
  ASSERT_EQ(one_bool->members.size(), 1);
  ASSERT_NO_FAILURES(CheckFieldShape(one_bool->members[0], ExpectedField{}));

  auto two_bools = test_library.LookupStruct("TwoBools");
  ASSERT_NOT_NULL(two_bools);
  ASSERT_NO_FAILURES(CheckTypeShape(two_bools, Expected{
                                                   .inline_size = 2,
                                                   .alignment = 1,
                                               }));
  ASSERT_EQ(two_bools->members.size(), 2);
  ASSERT_NO_FAILURES(CheckFieldShape(two_bools->members[0], ExpectedField{}));
  ASSERT_NO_FAILURES(CheckFieldShape(two_bools->members[1], ExpectedField{
                                                                .offset = 1,
                                                            }));

  auto bool_and_u32 = test_library.LookupStruct("BoolAndU32");
  ASSERT_NOT_NULL(bool_and_u32);
  ASSERT_NO_FAILURES(CheckTypeShape(bool_and_u32, Expected{
                                                      .inline_size = 8,
                                                      .alignment = 4,
                                                      .has_padding = true,
                                                  }));
  ASSERT_EQ(bool_and_u32->members.size(), 2);
  ASSERT_NO_FAILURES(CheckFieldShape(bool_and_u32->members[0], ExpectedField{.padding = 3}));
  ASSERT_NO_FAILURES(CheckFieldShape(bool_and_u32->members[1], ExpectedField{
                                                                   .offset = 4,
                                                               }));

  auto bool_and_u64 = test_library.LookupStruct("BoolAndU64");
  ASSERT_NOT_NULL(bool_and_u64);
  ASSERT_NO_FAILURES(CheckTypeShape(bool_and_u64, Expected{
                                                      .inline_size = 16,
                                                      .alignment = 8,
                                                      .has_padding = true,
                                                  }));
  ASSERT_EQ(bool_and_u64->members.size(), 2);
  ASSERT_NO_FAILURES(CheckFieldShape(bool_and_u64->members[0], ExpectedField{.padding = 7}));
  ASSERT_NO_FAILURES(CheckFieldShape(bool_and_u64->members[1], ExpectedField{
                                                                   .offset = 8,
                                                               }));
}

TEST(TypeshapeTests, simple_structs_with_handles) {
  TestLibrary test_library(kPrologWithHandleDefinition + R"FIDL(
struct OneHandle {
  handle h;
};

struct TwoHandles {
  handle:CHANNEL h1;
  handle:PORT h2;
};

struct ThreeHandlesOneOptional {
  handle:CHANNEL h1;
  handle:PORT h2;
  handle:TIMER? opt_h3;
};

    )FIDL");
  ASSERT_TRUE(test_library.Compile());

  auto one_handle = test_library.LookupStruct("OneHandle");
  ASSERT_NOT_NULL(one_handle);
  ASSERT_NO_FAILURES(CheckTypeShape(one_handle, Expected{
                                                    .inline_size = 4,
                                                    .alignment = 4,
                                                    .max_handles = 1,
                                                    .is_resource = true,
                                                }));
  ASSERT_EQ(one_handle->members.size(), 1);
  ASSERT_NO_FAILURES(CheckFieldShape(one_handle->members[0], ExpectedField{}));

  auto two_handles = test_library.LookupStruct("TwoHandles");
  ASSERT_NOT_NULL(two_handles);
  ASSERT_NO_FAILURES(CheckTypeShape(two_handles, Expected{
                                                     .inline_size = 8,
                                                     .alignment = 4,
                                                     .max_handles = 2,
                                                     .is_resource = true,
                                                 }));
  ASSERT_EQ(two_handles->members.size(), 2);
  ASSERT_NO_FAILURES(CheckFieldShape(two_handles->members[0], ExpectedField{}));
  ASSERT_NO_FAILURES(CheckFieldShape(two_handles->members[1], ExpectedField{
                                                                  .offset = 4,
                                                              }));

  auto three_handles_one_optional = test_library.LookupStruct("ThreeHandlesOneOptional");
  ASSERT_NOT_NULL(three_handles_one_optional);
  ASSERT_NO_FAILURES(CheckTypeShape(three_handles_one_optional, Expected{
                                                                    .inline_size = 12,
                                                                    .alignment = 4,
                                                                    .max_handles = 3,
                                                                    .is_resource = true,
                                                                }));
  ASSERT_EQ(three_handles_one_optional->members.size(), 3);
  ASSERT_NO_FAILURES(CheckFieldShape(three_handles_one_optional->members[0], ExpectedField{}));
  ASSERT_NO_FAILURES(CheckFieldShape(three_handles_one_optional->members[1], ExpectedField{
                                                                                 .offset = 4,
                                                                             }));
  ASSERT_NO_FAILURES(CheckFieldShape(three_handles_one_optional->members[2], ExpectedField{
                                                                                 .offset = 8,
                                                                             }));
}

TEST(TypeshapeTests, bits) {
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
  ASSERT_NOT_NULL(bits16);
  ASSERT_NO_FAILURES(CheckTypeShape(bits16, Expected{
                                                .inline_size = 2,
                                                .alignment = 2,
                                            }));

  auto bits_implicit = test_library.LookupBits("BitsImplicit");
  EXPECT_NOT_NULL(bits_implicit);
  ASSERT_NO_FAILURES(CheckTypeShape(bits_implicit, Expected{
                                                       .inline_size = 4,
                                                       .alignment = 4,
                                                   }));
}

TEST(TypeshapeTests, simple_tables) {
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
  ASSERT_NOT_NULL(no_members);
  ASSERT_NO_FAILURES(CheckTypeShape(no_members, Expected{
                                                    .inline_size = 16,
                                                    .alignment = 8,
                                                    .depth = 1,
                                                    .has_padding = false,
                                                    .has_flexible_envelope = true,
                                                }));

  auto one_bool = test_library.LookupTable("TableWithOneBool");
  ASSERT_NOT_NULL(one_bool);
  ASSERT_NO_FAILURES(CheckTypeShape(one_bool, Expected{
                                                  .inline_size = 16,
                                                  .alignment = 8,
                                                  .max_out_of_line = 24,
                                                  .depth = 2,
                                                  .has_padding = true,
                                                  .has_flexible_envelope = true,
                                              }));

  auto two_bools = test_library.LookupTable("TableWithTwoBools");
  ASSERT_NOT_NULL(two_bools);
  ASSERT_NO_FAILURES(CheckTypeShape(two_bools, Expected{
                                                   .inline_size = 16,
                                                   .alignment = 8,
                                                   .max_out_of_line = 48,
                                                   .depth = 2,
                                                   .has_padding = true,
                                                   .has_flexible_envelope = true,
                                               }));

  auto bool_and_u32 = test_library.LookupTable("TableWithBoolAndU32");
  ASSERT_NOT_NULL(bool_and_u32);
  ASSERT_NO_FAILURES(CheckTypeShape(bool_and_u32, Expected{
                                                      .inline_size = 16,
                                                      .alignment = 8,
                                                      .max_out_of_line = 48,
                                                      .depth = 2,
                                                      .has_padding = true,
                                                      .has_flexible_envelope = true,
                                                  }));

  auto bool_and_u64 = test_library.LookupTable("TableWithBoolAndU64");
  ASSERT_NOT_NULL(bool_and_u64);
  ASSERT_NO_FAILURES(CheckTypeShape(bool_and_u32, Expected{
                                                      .inline_size = 16,
                                                      .alignment = 8,
                                                      .max_out_of_line = 48,
                                                      .depth = 2,
                                                      .has_padding = true,
                                                      .has_flexible_envelope = true,
                                                  }));
}

TEST(TypeshapeTests, tables_with_reserved_fields) {
  TestLibrary test_library(R"FIDL(
library example;

table SomeReserved {
  1: bool b;
  2: reserved;
  3: bool b2;
  4: reserved;
};

table LastNonReserved {
  1: reserved;
  2: reserved;
  3: bool b;
};

table LastReserved {
  1: bool b;
  2: bool b2;
  3: reserved;
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
  ASSERT_NOT_NULL(some_reserved);
  ASSERT_NO_FAILURES(CheckTypeShape(some_reserved, Expected{
                                                       .inline_size = 16,
                                                       .alignment = 8,
                                                       .max_out_of_line = 64,
                                                       .depth = 2,
                                                       .has_padding = true,
                                                       .has_flexible_envelope = true,
                                                   }));

  auto last_non_reserved = test_library.LookupTable("LastNonReserved");
  ASSERT_NOT_NULL(last_non_reserved);
  ASSERT_NO_FAILURES(CheckTypeShape(last_non_reserved, Expected{
                                                           .inline_size = 16,
                                                           .alignment = 8,
                                                           .max_out_of_line = 56,
                                                           .depth = 2,
                                                           .has_padding = true,
                                                           .has_flexible_envelope = true,
                                                       }));

  auto last_reserved = test_library.LookupTable("LastReserved");
  ASSERT_NOT_NULL(last_reserved);
  ASSERT_NO_FAILURES(CheckTypeShape(last_reserved, Expected{
                                                       .inline_size = 16,
                                                       .alignment = 8,
                                                       .max_out_of_line = 48,
                                                       .depth = 2,
                                                       .has_padding = true,
                                                       .has_flexible_envelope = true,
                                                   }));

  auto all_reserved = test_library.LookupTable("AllReserved");
  ASSERT_NOT_NULL(all_reserved);
  ASSERT_NO_FAILURES(CheckTypeShape(all_reserved, Expected{
                                                      .inline_size = 16,
                                                      .alignment = 8,
                                                      .max_out_of_line = 0,
                                                      .depth = 1,
                                                      .has_padding = false,
                                                      .has_flexible_envelope = true,
                                                  }));

  auto one_reserved = test_library.LookupTable("OneReserved");
  ASSERT_NOT_NULL(one_reserved);
  ASSERT_NO_FAILURES(CheckTypeShape(one_reserved, Expected{
                                                      .inline_size = 16,
                                                      .alignment = 8,
                                                      .max_out_of_line = 0,
                                                      .depth = 1,
                                                      .has_padding = false,
                                                      .has_flexible_envelope = true,
                                                  }));
}

TEST(TypeshapeTests, simple_tables_with_handles) {
  TestLibrary test_library(R"FIDL(
library example;

table TableWithOneHandle {
  1: handle h;
};

    )FIDL");
  ASSERT_TRUE(test_library.Compile());

  auto one_handle = test_library.LookupTable("TableWithOneHandle");
  ASSERT_NOT_NULL(one_handle);
  ASSERT_NO_FAILURES(CheckTypeShape(one_handle, Expected{
                                                    .inline_size = 16,
                                                    .alignment = 8,
                                                    .max_out_of_line = 24,
                                                    .max_handles = 1,
                                                    .depth = 2,
                                                    .has_padding = true,
                                                    .has_flexible_envelope = true,
                                                    .is_resource = true,
                                                }));
}

TEST(TypeshapeTests, optional_structs) {
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
  ASSERT_NOT_NULL(one_bool);
  ASSERT_NO_FAILURES(CheckTypeShape(one_bool, Expected{
                                                  .inline_size = 8,
                                                  .alignment = 8,
                                                  .max_out_of_line = 8,
                                                  .depth = 1,
                                                  .has_padding = true,
                                              }));

  auto two_bools = test_library.LookupStruct("OptionalTwoBools");
  ASSERT_NOT_NULL(two_bools);
  ASSERT_NO_FAILURES(CheckTypeShape(two_bools, Expected{
                                                   .inline_size = 8,
                                                   .alignment = 8,
                                                   .max_out_of_line = 8,
                                                   .depth = 1,
                                                   .has_padding = true,
                                               }));

  auto bool_and_u32 = test_library.LookupStruct("OptionalBoolAndU32");
  ASSERT_NOT_NULL(bool_and_u32);
  ASSERT_NO_FAILURES(
      CheckTypeShape(bool_and_u32, Expected{
                                       .inline_size = 8,
                                       .alignment = 8,
                                       .max_out_of_line = 8,
                                       .depth = 1,
                                       .has_padding = true,  // because |BoolAndU32| has padding
                                   }));

  auto bool_and_u64 = test_library.LookupStruct("OptionalBoolAndU64");
  ASSERT_NOT_NULL(bool_and_u64);
  ASSERT_NO_FAILURES(
      CheckTypeShape(bool_and_u64, Expected{
                                       .inline_size = 8,
                                       .alignment = 8,
                                       .max_out_of_line = 16,
                                       .depth = 1,
                                       .has_padding = true,  // because |BoolAndU64| has padding
                                   }));
}

TEST(TypeshapeTests, optional_tables) {
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
  ASSERT_NOT_NULL(one_bool);
  ASSERT_NO_FAILURES(CheckTypeShape(one_bool, Expected{
                                                  .inline_size = 16,
                                                  .alignment = 8,
                                                  .max_out_of_line = 24,
                                                  .depth = 2,
                                                  .has_padding = true,
                                                  .has_flexible_envelope = true,
                                              }));

  auto table_with_one_bool = test_library.LookupTable("TableWithOptionalTableWithOneBool");
  ASSERT_NOT_NULL(table_with_one_bool);
  ASSERT_NO_FAILURES(CheckTypeShape(table_with_one_bool, Expected{
                                                             .inline_size = 16,
                                                             .alignment = 8,
                                                             .max_out_of_line = 56,
                                                             .depth = 4,
                                                             .has_padding = true,
                                                             .has_flexible_envelope = true,
                                                         }));

  auto two_bools = test_library.LookupTable("TableWithOptionalTwoBools");
  ASSERT_NOT_NULL(two_bools);
  ASSERT_NO_FAILURES(CheckTypeShape(two_bools, Expected{
                                                   .inline_size = 16,
                                                   .alignment = 8,
                                                   .max_out_of_line = 24,
                                                   .depth = 2,
                                                   .has_padding = true,
                                                   .has_flexible_envelope = true,
                                               }));

  auto table_with_two_bools = test_library.LookupTable("TableWithOptionalTableWithTwoBools");
  ASSERT_NOT_NULL(table_with_two_bools);
  ASSERT_NO_FAILURES(CheckTypeShape(table_with_two_bools, Expected{
                                                              .inline_size = 16,
                                                              .alignment = 8,
                                                              .max_out_of_line = 80,
                                                              .depth = 4,
                                                              .has_padding = true,
                                                              .has_flexible_envelope = true,
                                                          }));

  auto bool_and_u32 = test_library.LookupTable("TableWithOptionalBoolAndU32");
  ASSERT_NOT_NULL(bool_and_u32);
  ASSERT_NO_FAILURES(CheckTypeShape(bool_and_u32, Expected{
                                                      .inline_size = 16,
                                                      .alignment = 8,
                                                      .max_out_of_line = 24,
                                                      .depth = 2,
                                                      .has_padding = true,
                                                      .has_flexible_envelope = true,
                                                  }));

  auto table_with_bool_and_u32 = test_library.LookupTable("TableWithOptionalTableWithBoolAndU32");
  ASSERT_NOT_NULL(table_with_bool_and_u32);
  ASSERT_NO_FAILURES(CheckTypeShape(table_with_bool_and_u32, Expected{
                                                                 .inline_size = 16,
                                                                 .alignment = 8,
                                                                 .max_out_of_line = 80,
                                                                 .depth = 4,
                                                                 .has_padding = true,
                                                                 .has_flexible_envelope = true,
                                                             }));

  auto bool_and_u64 = test_library.LookupTable("TableWithOptionalBoolAndU64");
  ASSERT_NOT_NULL(bool_and_u64);
  ASSERT_NO_FAILURES(CheckTypeShape(bool_and_u64, Expected{
                                                      .inline_size = 16,
                                                      .alignment = 8,
                                                      .max_out_of_line = 32,
                                                      .depth = 2,
                                                      .has_padding = true,
                                                      .has_flexible_envelope = true,
                                                  }));

  auto table_with_bool_and_u64 = test_library.LookupTable("TableWithOptionalTableWithBoolAndU64");
  ASSERT_NOT_NULL(table_with_bool_and_u64);
  ASSERT_NO_FAILURES(CheckTypeShape(table_with_bool_and_u64, Expected{
                                                                 .inline_size = 16,
                                                                 .alignment = 8,
                                                                 .max_out_of_line = 80,
                                                                 .depth = 4,
                                                                 .has_padding = true,
                                                                 .has_flexible_envelope = true,
                                                             }));
}

TEST(TypeshapeTests, unions) {
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
  ASSERT_NO_FAILURES(CheckTypeShape(union_with_out_of_line,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 8,
                                        .depth = 1,
                                        .has_padding = true,
                                    },
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 16,
                                        .depth = 2,
                                        .has_padding = true,
                                    }));

  auto a_union = test_library.LookupUnion("UnionOfThings");
  ASSERT_NOT_NULL(a_union);
  ASSERT_NO_FAILURES(CheckTypeShape(a_union,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .has_padding = true,
                                    },
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 16,
                                        .depth = 1,
                                        .has_padding = true,
                                    }));
  ASSERT_EQ(a_union->members.size(), 2);
  ASSERT_NOT_NULL(a_union->members[0].maybe_used);
  ASSERT_NO_FAILURES(
      CheckFieldShape(*a_union->members[0].maybe_used,
                      ExpectedField{
                          .offset = 8,
                          .padding = 15  // The other variant, |BoolAndU64|, has a size of 16 bytes.
                      },
                      ExpectedField{
                          .offset = 0,
                          .padding = 7,
                      }));
  ASSERT_NOT_NULL(a_union->members[1].maybe_used);
  ASSERT_NO_FAILURES(CheckFieldShape(*a_union->members[1].maybe_used,
                                     ExpectedField{
                                         .offset = 8,
                                         .padding = 0  // This is the biggest variant.
                                     },
                                     ExpectedField{}));

  auto optional_union = test_library.LookupStruct("OptionalUnion");
  ASSERT_NOT_NULL(optional_union);
  ASSERT_NO_FAILURES(CheckTypeShape(optional_union,
                                    Expected{
                                        .inline_size = 8,
                                        .alignment = 8,
                                        .max_out_of_line = 24,
                                        .depth = 1,
                                        .has_padding = true,  // because |UnionOfThings| has padding
                                    },
                                    Expected{
                                        // because |UnionOfThings| union header is inline
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 16,
                                        .depth = 1,
                                        .has_padding = true,
                                    }));

  auto table_with_optional_union = test_library.LookupTable("TableWithOptionalUnion");
  ASSERT_NOT_NULL(table_with_optional_union);
  ASSERT_NO_FAILURES(CheckTypeShape(table_with_optional_union,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 40,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 56,
                                        .depth = 3,
                                        .has_padding = true,
                                        .has_flexible_envelope = true,
                                    }));
}

TEST(TypeshapeTests, unions_with_handles) {
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
  ASSERT_NOT_NULL(one_handle_union);
  ASSERT_NO_FAILURES(CheckTypeShape(one_handle_union,
                                    Expected{
                                        .inline_size = 8,
                                        .alignment = 4,
                                        .max_handles = 1,
                                        .has_padding = true,
                                        .is_resource = true,
                                    },
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 8,
                                        .max_handles = 1,
                                        .depth = 1,
                                        .has_padding = true,
                                        .is_resource = true,
                                    }));
  ASSERT_EQ(one_handle_union->members.size(), 3);
  ASSERT_NOT_NULL(one_handle_union->members[0].maybe_used);
  ASSERT_NO_FAILURES(CheckFieldShape(*one_handle_union->members[0].maybe_used,
                                     ExpectedField{
                                         .offset = 4,
                                         .padding = 0  // This is the biggest variant.
                                     },
                                     ExpectedField{
                                         .offset = 0,
                                         .padding = 4,
                                     }));
  ASSERT_NOT_NULL(one_handle_union->members[1].maybe_used);
  ASSERT_NO_FAILURES(CheckFieldShape(*one_handle_union->members[1].maybe_used,
                                     ExpectedField{
                                         .offset = 4,
                                         .padding = 3  // The other variants all have size of 4.
                                     },
                                     ExpectedField{
                                         .offset = 0,
                                         .padding = 7,
                                     }));
  ASSERT_NOT_NULL(one_handle_union->members[2].maybe_used);
  ASSERT_NO_FAILURES(CheckFieldShape(*one_handle_union->members[2].maybe_used,
                                     ExpectedField{
                                         .offset = 4,
                                         .padding = 0  // This is the biggest variant.
                                     },
                                     ExpectedField{
                                         .offset = 0,
                                         .padding = 4,
                                     }));

  auto many_handle_union = test_library.LookupUnion("ManyHandleUnion");
  ASSERT_NOT_NULL(many_handle_union);
  ASSERT_NO_FAILURES(CheckTypeShape(many_handle_union,
                                    Expected{
                                        .inline_size = 40,
                                        .alignment = 8,
                                        .max_out_of_line = 32,
                                        .max_handles = 8,
                                        .depth = 1,
                                        .has_padding = true,
                                        .is_resource = true,
                                    },
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 48,
                                        .max_handles = 8,
                                        .depth = 2,
                                        .has_padding = true,
                                        .is_resource = true,
                                    }));
  ASSERT_EQ(many_handle_union->members.size(), 3);
  ASSERT_NOT_NULL(many_handle_union->members[1].maybe_used);
  ASSERT_NO_FAILURES(CheckFieldShape(
      *many_handle_union->members[0].maybe_used,
      ExpectedField{
          .offset = 8,
          .padding = 28  // The biggest variant, |array<handle>:8|, has a size of 32.
      },
      ExpectedField{
          .offset = 0,
          .padding = 4,
      }));
  ASSERT_NOT_NULL(many_handle_union->members[1].maybe_used);
  ASSERT_NO_FAILURES(CheckFieldShape(*many_handle_union->members[1].maybe_used,
                                     ExpectedField{
                                         .offset = 8,
                                         .padding = 0  // This is the biggest variant.
                                     },
                                     ExpectedField{}));
  ASSERT_NOT_NULL(many_handle_union->members[2].maybe_used);
  ASSERT_NO_FAILURES(CheckFieldShape(
      *many_handle_union->members[2].maybe_used,
      ExpectedField{
          .offset = 8,
          .padding = 16  // This biggest variant, |array<handle>:8|, has a size of 32.
      },
      ExpectedField{}));
}

TEST(TypeshapeTests, vectors) {
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
  ASSERT_NOT_NULL(padded_vector);
  ASSERT_NO_FAILURES(CheckTypeShape(padded_vector, Expected{
                                                       .inline_size = 16,
                                                       .alignment = 8,
                                                       .max_out_of_line = 16,
                                                       .depth = 1,
                                                       .has_padding = true,
                                                   }));

  auto no_padding_vector = test_library.LookupStruct("NoPaddingVector");
  ASSERT_NOT_NULL(no_padding_vector);
  ASSERT_NO_FAILURES(CheckTypeShape(no_padding_vector, Expected{
                                                           .inline_size = 16,
                                                           .alignment = 8,
                                                           .max_out_of_line = 24,
                                                           .depth = 1,
                                                           .has_padding = false,
                                                       }));

  auto unbounded_vector = test_library.LookupStruct("UnboundedVector");
  ASSERT_NOT_NULL(unbounded_vector);
  ASSERT_NO_FAILURES(
      CheckTypeShape(unbounded_vector, Expected{
                                           .inline_size = 16,
                                           .alignment = 8,
                                           .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                           .depth = 1,
                                           .has_padding = true,
                                       }));

  auto unbounded_vectors = test_library.LookupStruct("UnboundedVectors");
  ASSERT_NOT_NULL(unbounded_vectors);
  ASSERT_NO_FAILURES(
      CheckTypeShape(unbounded_vectors, Expected{
                                            .inline_size = 32,
                                            .alignment = 8,
                                            .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                            .depth = 1,
                                            .has_padding = true,
                                        }));

  auto table_with_padded_vector = test_library.LookupTable("TableWithPaddedVector");
  ASSERT_NOT_NULL(table_with_padded_vector);
  ASSERT_NO_FAILURES(CheckTypeShape(table_with_padded_vector, Expected{
                                                                  .inline_size = 16,
                                                                  .alignment = 8,
                                                                  .max_out_of_line = 48,
                                                                  .depth = 3,
                                                                  .has_padding = true,
                                                                  .has_flexible_envelope = true,
                                                              }));

  auto table_with_unbounded_vector = test_library.LookupTable("TableWithUnboundedVector");
  ASSERT_NOT_NULL(table_with_unbounded_vector);
  ASSERT_NO_FAILURES(CheckTypeShape(table_with_unbounded_vector,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                        .depth = 3,
                                        .has_padding = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto table_with_unbounded_vectors = test_library.LookupTable("TableWithUnboundedVectors");
  ASSERT_NOT_NULL(table_with_unbounded_vectors);
  ASSERT_NO_FAILURES(CheckTypeShape(table_with_unbounded_vectors,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                        .depth = 3,
                                        .has_padding = true,
                                        .has_flexible_envelope = true,
                                    }));
}

TEST(TypeshapeTests, vectors_with_handles) {
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
  ASSERT_NOT_NULL(handle_vector);
  ASSERT_NO_FAILURES(CheckTypeShape(handle_vector, Expected{
                                                       .inline_size = 16,
                                                       .alignment = 8,
                                                       .max_out_of_line = 32,
                                                       .max_handles = 8,
                                                       .depth = 1,
                                                       .has_padding = true,
                                                       .is_resource = true,
                                                   }));

  auto handle_nullable_vector = test_library.LookupStruct("HandleNullableVector");
  ASSERT_NOT_NULL(handle_nullable_vector);
  ASSERT_NO_FAILURES(CheckTypeShape(handle_nullable_vector, Expected{
                                                                .inline_size = 16,
                                                                .alignment = 8,
                                                                .max_out_of_line = 32,
                                                                .max_handles = 8,
                                                                .depth = 1,
                                                                .has_padding = true,
                                                                .is_resource = true,
                                                            }));

  auto unbounded_handle_vector = test_library.LookupStruct("UnboundedHandleVector");
  ASSERT_NOT_NULL(unbounded_handle_vector);
  ASSERT_NO_FAILURES(CheckTypeShape(unbounded_handle_vector,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                        .max_handles = std::numeric_limits<uint32_t>::max(),
                                        .depth = 1,
                                        .has_padding = true,
                                        .is_resource = true,
                                    }));

  auto table_with_unbounded_handle_vector =
      test_library.LookupTable("TableWithUnboundedHandleVector");
  ASSERT_NOT_NULL(table_with_unbounded_handle_vector);
  ASSERT_NO_FAILURES(CheckTypeShape(table_with_unbounded_handle_vector,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                        .max_handles = std::numeric_limits<uint32_t>::max(),
                                        .depth = 3,
                                        .has_padding = true,
                                        .has_flexible_envelope = true,
                                        .is_resource = true,
                                    }));

  auto handle_struct_vector = test_library.LookupStruct("HandleStructVector");
  ASSERT_NOT_NULL(handle_struct_vector);
  ASSERT_NO_FAILURES(CheckTypeShape(handle_struct_vector, Expected{
                                                              .inline_size = 16,
                                                              .alignment = 8,
                                                              .max_out_of_line = 32,
                                                              .max_handles = 8,
                                                              .depth = 1,
                                                              .has_padding = true,
                                                              .is_resource = true,
                                                          }));

  auto handle_table_vector = test_library.LookupStruct("HandleTableVector");
  ASSERT_NOT_NULL(handle_table_vector);
  ASSERT_NO_FAILURES(CheckTypeShape(handle_table_vector, Expected{
                                                             .inline_size = 16,
                                                             .alignment = 8,
                                                             .max_out_of_line = 320,
                                                             .max_handles = 8,
                                                             .depth = 3,
                                                             .has_padding = true,
                                                             .has_flexible_envelope = true,
                                                             .is_resource = true,
                                                         }));

  auto table_with_handle_struct_vector = test_library.LookupTable("TableWithHandleStructVector");
  ASSERT_NOT_NULL(table_with_handle_struct_vector);
  ASSERT_NO_FAILURES(
      CheckTypeShape(table_with_handle_struct_vector, Expected{
                                                          .inline_size = 16,
                                                          .alignment = 8,
                                                          .max_out_of_line = 64,
                                                          .max_handles = 8,
                                                          .depth = 3,
                                                          .has_padding = true,
                                                          .has_flexible_envelope = true,
                                                          .is_resource = true,
                                                      }));
}

TEST(TypeshapeTests, strings) {
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
  ASSERT_NOT_NULL(short_string);
  ASSERT_NO_FAILURES(CheckTypeShape(short_string, Expected{
                                                      .inline_size = 16,
                                                      .alignment = 8,
                                                      .max_out_of_line = 8,
                                                      .depth = 1,
                                                      .has_padding = true,
                                                  }));

  auto unbounded_string = test_library.LookupStruct("UnboundedString");
  ASSERT_NOT_NULL(unbounded_string);
  ASSERT_NO_FAILURES(
      CheckTypeShape(unbounded_string, Expected{
                                           .inline_size = 16,
                                           .alignment = 8,
                                           .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                           .depth = 1,
                                           .has_padding = true,
                                       }));

  auto table_with_short_string = test_library.LookupTable("TableWithShortString");
  ASSERT_NOT_NULL(table_with_short_string);
  ASSERT_NO_FAILURES(CheckTypeShape(table_with_short_string, Expected{
                                                                 .inline_size = 16,
                                                                 .alignment = 8,
                                                                 .max_out_of_line = 40,
                                                                 .depth = 3,
                                                                 .has_padding = true,
                                                                 .has_flexible_envelope = true,
                                                             }));

  auto table_with_unbounded_string = test_library.LookupTable("TableWithUnboundedString");
  ASSERT_NOT_NULL(table_with_unbounded_string);
  ASSERT_NO_FAILURES(CheckTypeShape(table_with_unbounded_string,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                        .depth = 3,
                                        .has_padding = true,
                                        .has_flexible_envelope = true,
                                    }));
}

TEST(TypeshapeTests, arrays) {
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
  ASSERT_NOT_NULL(an_array);
  ASSERT_NO_FAILURES(CheckTypeShape(an_array, Expected{
                                                  .inline_size = 40,
                                                  .alignment = 8,
                                              }));

  auto table_with_an_array = test_library.LookupTable("TableWithAnArray");
  ASSERT_NOT_NULL(table_with_an_array);
  ASSERT_NO_FAILURES(CheckTypeShape(table_with_an_array, Expected{
                                                             .inline_size = 16,
                                                             .alignment = 8,
                                                             .max_out_of_line = 56,
                                                             .depth = 2,
                                                             .has_padding = false,
                                                             .has_flexible_envelope = true,
                                                         }));

  auto table_with_an_int32_array_with_padding =
      test_library.LookupTable("TableWithAnInt32ArrayWithPadding");
  ASSERT_NOT_NULL(table_with_an_int32_array_with_padding);
  ASSERT_NO_FAILURES(
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
  ASSERT_NOT_NULL(table_with_an_int32_array_no_padding);
  ASSERT_NO_FAILURES(
      CheckTypeShape(table_with_an_int32_array_no_padding,
                     Expected{
                         .inline_size = 16,
                         .alignment = 8,
                         .max_out_of_line = 32,  // 16 table header + ALIGN(4 * 4 array) = 32
                         .depth = 2,
                         .has_padding = false,
                         .has_flexible_envelope = true,
                     }));
}

TEST(TypeshapeTests, arrays_with_handles) {
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
  ASSERT_NOT_NULL(handle_array);
  ASSERT_NO_FAILURES(CheckTypeShape(handle_array, Expected{
                                                      .inline_size = 32,
                                                      .alignment = 4,
                                                      .max_handles = 8,
                                                      .is_resource = true,
                                                  }));

  auto table_with_handle_array = test_library.LookupTable("TableWithHandleArray");
  ASSERT_NOT_NULL(table_with_handle_array);
  ASSERT_NO_FAILURES(CheckTypeShape(table_with_handle_array, Expected{
                                                                 .inline_size = 16,
                                                                 .alignment = 8,
                                                                 .max_out_of_line = 48,
                                                                 .max_handles = 8,
                                                                 .depth = 2,
                                                                 .has_padding = false,
                                                                 .has_flexible_envelope = true,
                                                                 .is_resource = true,
                                                             }));

  auto nullable_handle_array = test_library.LookupStruct("NullableHandleArray");
  ASSERT_NOT_NULL(nullable_handle_array);
  ASSERT_NO_FAILURES(CheckTypeShape(nullable_handle_array, Expected{
                                                               .inline_size = 32,
                                                               .alignment = 4,
                                                               .max_handles = 8,
                                                               .is_resource = true,
                                                           }));

  auto table_with_nullable_handle_array = test_library.LookupTable("TableWithNullableHandleArray");
  ASSERT_NOT_NULL(table_with_nullable_handle_array);
  ASSERT_NO_FAILURES(
      CheckTypeShape(table_with_nullable_handle_array, Expected{
                                                           .inline_size = 16,
                                                           .alignment = 8,
                                                           .max_out_of_line = 48,
                                                           .max_handles = 8,
                                                           .depth = 2,
                                                           .has_padding = false,
                                                           .has_flexible_envelope = true,
                                                           .is_resource = true,
                                                       }));
}

// TODO(pascallouis): write an "xunions_with_handles" test case.

TEST(TypeshapeTests, flexible_unions) {
  TestLibrary test_library(R"FIDL(
library example;

flexible union XUnionWithOneBool {
  1: bool b;
};

struct StructWithOptionalXUnionWithOneBool {
  XUnionWithOneBool? opt_xunion_with_bool;
};

flexible union XUnionWithBoundedOutOfLineObject {
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

flexible union XUnionWithUnboundedOutOfLineObject {
  1: string s;
};

flexible union XUnionWithoutPayloadPadding {
  1: array<uint64>:7 a;
};

flexible union PaddingCheck {
  1: array<uint8>:3 three;
  2: array<uint8>:5 five;
};
    )FIDL");
  ASSERT_TRUE(test_library.Compile());

  auto one_bool = test_library.LookupUnion("XUnionWithOneBool");
  ASSERT_NOT_NULL(one_bool);
  ASSERT_NO_FAILURES(CheckTypeShape(one_bool,
                                    Expected{
                                        .inline_size = 8,
                                        .alignment = 4,
                                        .max_out_of_line = 0,
                                        .depth = 0,
                                        .has_padding = true,
                                    },
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 8,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_flexible_envelope = true,
                                    }));
  ASSERT_EQ(one_bool->members.size(), 1);
  ASSERT_NOT_NULL(one_bool->members[0].maybe_used);
  ASSERT_NO_FAILURES(CheckFieldShape(*one_bool->members[0].maybe_used,
                                     ExpectedField{.offset = 4, .padding = 3},
                                     ExpectedField{.padding = 7}));

  auto opt_one_bool = test_library.LookupStruct("StructWithOptionalXUnionWithOneBool");
  ASSERT_NOT_NULL(opt_one_bool);
  ASSERT_NO_FAILURES(CheckTypeShape(opt_one_bool,
                                    Expected{
                                        .inline_size = 8,
                                        .alignment = 8,
                                        .max_out_of_line = 8,
                                        .depth = 1,
                                        .has_padding = true,
                                    },
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 8,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto xu = test_library.LookupUnion("XUnionWithBoundedOutOfLineObject");
  ASSERT_NOT_NULL(xu);
  ASSERT_NO_FAILURES(CheckTypeShape(xu,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 240,
                                        .depth = 2,
                                        .has_padding = true,
                                    },
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 256,
                                        .depth = 3,
                                        .has_padding = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto unbounded = test_library.LookupUnion("XUnionWithUnboundedOutOfLineObject");
  ASSERT_NOT_NULL(unbounded);
  ASSERT_NO_FAILURES(CheckTypeShape(unbounded,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                        .depth = 1,
                                        .has_padding = true,
                                    },
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto xu_no_payload_padding = test_library.LookupUnion("XUnionWithoutPayloadPadding");
  ASSERT_NOT_NULL(xu_no_payload_padding);
  ASSERT_NO_FAILURES(
      CheckTypeShape(xu_no_payload_padding,
                     Expected{
                         .inline_size = 64,
                         .alignment = 8,
                         .max_out_of_line = 0,
                         .depth = 0,
                         .has_padding = true,
                     },
                     Expected{
                         .inline_size = 24,
                         .alignment = 8,
                         .max_out_of_line = 56,
                         .depth = 1,
                         // xunion always have padding, because its ordinal is 32 bits.
                         // TODO(fxbug.dev/7970): increase the ordinal size to 64 bits, such that
                         // there is no padding.
                         .has_padding = true,
                         .has_flexible_envelope = true,
                     }));

  auto padding_check = test_library.LookupUnion("PaddingCheck");
  ASSERT_NOT_NULL(padding_check);
  ASSERT_NO_FAILURES(CheckTypeShape(padding_check,
                                    Expected{
                                        .inline_size = 12,
                                        .alignment = 4,
                                        .max_out_of_line = 0,
                                        .depth = 0,
                                        .has_padding = true,
                                    },
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 8,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_flexible_envelope = true,
                                    }));
  ASSERT_EQ(padding_check->members.size(), 2);
  ASSERT_NOT_NULL(padding_check->members[0].maybe_used);
  ASSERT_NO_FAILURES(CheckFieldShape(*padding_check->members[0].maybe_used,
                                     ExpectedField{.offset = 4, .padding = 5},
                                     ExpectedField{.padding = 5}));
  ASSERT_NO_FAILURES(CheckFieldShape(*padding_check->members[1].maybe_used,
                                     ExpectedField{.offset = 4, .padding = 3},
                                     ExpectedField{.padding = 3}));
}

TEST(TypeshapeTests, envelope_strictness) {
  TestLibrary test_library(R"FIDL(
library example;

strict union StrictLeafXUnion {
    1: int64 a;
};

flexible union FlexibleLeafXUnion {
    1: int64 a;
};

flexible union FlexibleXUnionOfStrictXUnion {
    1: StrictLeafXUnion xu;
};

flexible union FlexibleXUnionOfFlexibleXUnion {
    1: FlexibleLeafXUnion xu;
};

strict union StrictXUnionOfStrictXUnion {
    1: StrictLeafXUnion xu;
};

strict union StrictXUnionOfFlexibleXUnion {
    1: FlexibleLeafXUnion xu;
};

table FlexibleLeafTable {
};

strict union StrictXUnionOfFlexibleTable {
    1: FlexibleLeafTable ft;
};

    )FIDL");
  ASSERT_TRUE(test_library.Compile());

  auto strict_xunion = test_library.LookupUnion("StrictLeafXUnion");
  ASSERT_NOT_NULL(strict_xunion);
  ASSERT_NO_FAILURES(CheckTypeShape(strict_xunion,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 0,
                                        .depth = 0,
                                        .has_padding = true,
                                    },
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 8,
                                        .depth = 1,
                                        .has_padding = true,
                                    }));

  auto flexible_xunion = test_library.LookupUnion("FlexibleLeafXUnion");
  ASSERT_NOT_NULL(flexible_xunion);
  ASSERT_NO_FAILURES(CheckTypeShape(flexible_xunion,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 0,
                                        .depth = 0,
                                        .has_padding = true,
                                    },
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 8,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto flexible_of_strict = test_library.LookupUnion("FlexibleXUnionOfStrictXUnion");
  ASSERT_NOT_NULL(flexible_of_strict);
  ASSERT_NO_FAILURES(CheckTypeShape(flexible_of_strict,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 0,
                                        .depth = 0,
                                        .has_padding = true,
                                    },
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 32,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto flexible_of_flexible = test_library.LookupUnion("FlexibleXUnionOfFlexibleXUnion");
  ASSERT_NOT_NULL(flexible_of_flexible);
  ASSERT_NO_FAILURES(CheckTypeShape(flexible_of_flexible,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 0,
                                        .depth = 0,
                                        .has_padding = true,
                                    },
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 32,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto strict_of_strict = test_library.LookupUnion("StrictXUnionOfStrictXUnion");
  ASSERT_NOT_NULL(strict_of_strict);
  ASSERT_NO_FAILURES(CheckTypeShape(strict_of_strict,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 0,
                                        .depth = 0,
                                        .has_padding = true,
                                    },
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 32,
                                        .depth = 2,
                                        .has_padding = true,
                                    }));

  auto strict_of_flexible = test_library.LookupUnion("StrictXUnionOfFlexibleXUnion");
  ASSERT_NOT_NULL(strict_of_flexible);
  ASSERT_NO_FAILURES(CheckTypeShape(strict_of_flexible,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 0,
                                        .depth = 0,
                                        .has_padding = true,
                                    },
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 32,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto flexible_table = test_library.LookupTable("FlexibleLeafTable");
  ASSERT_NOT_NULL(flexible_table);
  ASSERT_NO_FAILURES(CheckTypeShape(flexible_table, Expected{
                                                        .inline_size = 16,
                                                        .alignment = 8,
                                                        .max_out_of_line = 0,
                                                        .depth = 1,
                                                        .has_padding = false,
                                                        .has_flexible_envelope = true,
                                                    }));

  auto strict_xunion_of_flexible_table = test_library.LookupUnion("StrictXUnionOfFlexibleTable");
  ASSERT_NOT_NULL(strict_xunion_of_flexible_table);
  ASSERT_NO_FAILURES(CheckTypeShape(strict_xunion_of_flexible_table,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 0,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 16,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_flexible_envelope = true,
                                    }));
}

TEST(TypeshapeTests, protocols_and_request_of_protocols) {
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
  ASSERT_NOT_NULL(using_some_protocol);
  ASSERT_NO_FAILURES(CheckTypeShape(using_some_protocol, Expected{
                                                             .inline_size = 4,
                                                             .alignment = 4,
                                                             .max_handles = 1,
                                                             .is_resource = true,
                                                         }));

  auto using_opt_some_protocol = test_library.LookupStruct("UsingOptSomeProtocol");
  ASSERT_NOT_NULL(using_opt_some_protocol);
  ASSERT_NO_FAILURES(CheckTypeShape(using_opt_some_protocol, Expected{
                                                                 .inline_size = 4,
                                                                 .alignment = 4,
                                                                 .max_handles = 1,
                                                                 .is_resource = true,
                                                             }));

  auto using_request_some_protocol = test_library.LookupStruct("UsingRequestSomeProtocol");
  ASSERT_NOT_NULL(using_request_some_protocol);
  ASSERT_NO_FAILURES(CheckTypeShape(using_request_some_protocol, Expected{
                                                                     .inline_size = 4,
                                                                     .alignment = 4,
                                                                     .max_handles = 1,
                                                                     .is_resource = true,
                                                                 }));

  auto using_opt_request_some_protocol = test_library.LookupStruct("UsingOptRequestSomeProtocol");
  ASSERT_NOT_NULL(using_opt_request_some_protocol);
  ASSERT_NO_FAILURES(CheckTypeShape(using_opt_request_some_protocol, Expected{
                                                                         .inline_size = 4,
                                                                         .alignment = 4,
                                                                         .max_handles = 1,
                                                                         .is_resource = true,
                                                                     }));
}

TEST(TypeshapeTests, external_definitions) {
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
  ASSERT_NOT_NULL(ext_struct);
  ASSERT_NO_FAILURES(CheckTypeShape(ext_struct, Expected{
                                                    .inline_size = 4,
                                                    .alignment = 4,
                                                }));

  auto ext_arr_struct = test_library.LookupStruct("ExternalArrayStruct");
  ASSERT_NOT_NULL(ext_arr_struct);
  ASSERT_NO_FAILURES(CheckTypeShape(ext_arr_struct, Expected{
                                                        .inline_size = 4 * 32,
                                                        .alignment = 4,
                                                    }));

  auto ext_str_struct = test_library.LookupStruct("ExternalStringSizeStruct");
  ASSERT_NOT_NULL(ext_str_struct);
  ASSERT_NO_FAILURES(CheckTypeShape(ext_str_struct, Expected{
                                                        .inline_size = 16,
                                                        .alignment = 8,
                                                        .max_out_of_line = 32,
                                                        .depth = 1,
                                                        .has_padding = true,
                                                    }));

  auto ext_vec_struct = test_library.LookupStruct("ExternalVectorSizeStruct");
  ASSERT_NOT_NULL(ext_vec_struct);
  ASSERT_NO_FAILURES(CheckTypeShape(ext_vec_struct, Expected{
                                                        .inline_size = 16,
                                                        .alignment = 8,
                                                        .max_out_of_line = 32 * 4,
                                                        .max_handles = 32,
                                                        .depth = 1,
                                                        .has_padding = true,
                                                        .is_resource = true,
                                                    }));
}

TEST(TypeshapeTests, recursive_request) {
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
  ASSERT_NOT_NULL(web_message);
  ASSERT_NO_FAILURES(CheckTypeShape(web_message, Expected{
                                                     .inline_size = 4,
                                                     .alignment = 4,
                                                     .max_handles = 1,
                                                     .is_resource = true,
                                                 }));
  ASSERT_EQ(web_message->members.size(), 1);
  ASSERT_NO_FAILURES(CheckFieldShape(web_message->members[0], ExpectedField{}));

  auto message_port = library.LookupProtocol("MessagePort");
  ASSERT_NOT_NULL(message_port);
  ASSERT_EQ(message_port->methods.size(), 1);
  auto& post_message = message_port->methods[0];
  auto post_message_request = post_message.maybe_request;
  ASSERT_NOT_NULL(post_message_request);
  ASSERT_NO_FAILURES(CheckTypeShape(post_message_request,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_handles = 1,
                                        .has_padding = true,
                                        .is_resource = true,
                                    },
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_handles = 1,
                                        .has_padding = true,
                                        .is_resource = true,
                                    },
                                    Expected{
                                        .inline_size = 8,
                                        .alignment = 8,
                                        .max_handles = 1,
                                        .has_padding = true,
                                        .is_resource = true,
                                    }));
  ASSERT_EQ(post_message_request->members.size(), 1);
  ASSERT_NO_FAILURES(
      CheckFieldShape(post_message_request->members[0], ExpectedField{.offset = 16, .padding = 4}));
}

TEST(TypeshapeTests, recursive_opt_request) {
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
  ASSERT_NOT_NULL(web_message);
  ASSERT_NO_FAILURES(CheckTypeShape(web_message, Expected{
                                                     .inline_size = 4,
                                                     .alignment = 4,
                                                     .max_handles = 1,
                                                     .is_resource = true,
                                                 }));

  auto message_port = library.LookupProtocol("MessagePort");
  ASSERT_NOT_NULL(message_port);
  ASSERT_EQ(message_port->methods.size(), 1);
  auto& post_message = message_port->methods[0];
  auto post_message_request = post_message.maybe_request;
  ASSERT_NOT_NULL(post_message_request);
  ASSERT_NO_FAILURES(CheckTypeShape(post_message_request,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_handles = 1,
                                        .has_padding = true,
                                        .is_resource = true,
                                    },
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_handles = 1,
                                        .has_padding = true,
                                        .is_resource = true,
                                    },
                                    Expected{
                                        .inline_size = 8,
                                        .alignment = 8,
                                        .max_handles = 1,
                                        .has_padding = true,
                                        .is_resource = true,
                                    }));
}

TEST(TypeshapeTests, recursive_protocol) {
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
  ASSERT_NOT_NULL(web_message);
  ASSERT_NO_FAILURES(CheckTypeShape(web_message, Expected{
                                                     .inline_size = 4,
                                                     .alignment = 4,
                                                     .max_handles = 1,
                                                     .is_resource = true,
                                                 }));

  auto message_port = library.LookupProtocol("MessagePort");
  ASSERT_NOT_NULL(message_port);
  ASSERT_EQ(message_port->methods.size(), 1);
  auto& post_message = message_port->methods[0];
  auto post_message_request = post_message.maybe_request;
  ASSERT_NOT_NULL(post_message_request);
  ASSERT_NO_FAILURES(CheckTypeShape(post_message_request,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_handles = 1,
                                        .has_padding = true,
                                        .is_resource = true,
                                    },
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_handles = 1,
                                        .has_padding = true,
                                        .is_resource = true,
                                    },
                                    Expected{
                                        .inline_size = 8,
                                        .alignment = 8,
                                        .max_handles = 1,
                                        .has_padding = true,
                                        .is_resource = true,
                                    }));
}

TEST(TypeshapeTests, recursive_opt_protocol) {
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
  ASSERT_NOT_NULL(web_message);
  ASSERT_NO_FAILURES(CheckTypeShape(web_message, Expected{
                                                     .inline_size = 4,
                                                     .alignment = 4,
                                                     .max_handles = 1,
                                                     .is_resource = true,
                                                 }));

  auto message_port = library.LookupProtocol("MessagePort");
  ASSERT_NOT_NULL(message_port);
  ASSERT_EQ(message_port->methods.size(), 1);
  auto& post_message = message_port->methods[0];
  auto post_message_request = post_message.maybe_request;
  ASSERT_NOT_NULL(post_message_request);
  ASSERT_NO_FAILURES(CheckTypeShape(post_message_request,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_handles = 1,
                                        .has_padding = true,
                                        .is_resource = true,
                                    },
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_handles = 1,
                                        .has_padding = true,
                                        .is_resource = true,
                                    },
                                    Expected{
                                        .inline_size = 8,
                                        .alignment = 8,
                                        .max_handles = 1,
                                        .has_padding = true,
                                        .is_resource = true,
                                    }));
}

TEST(TypeshapeTests, recursive_struct) {
  TestLibrary library(R"FIDL(
library example;

struct TheStruct {
  TheStruct? opt_one_more;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto the_struct = library.LookupStruct("TheStruct");
  ASSERT_NOT_NULL(the_struct);
  ASSERT_NO_FAILURES(
      CheckTypeShape(the_struct, Expected{
                                     .inline_size = 8,
                                     .alignment = 8,
                                     .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                     .max_handles = 0,
                                     .depth = std::numeric_limits<uint32_t>::max(),
                                 }));
  ASSERT_EQ(the_struct->members.size(), 1);
  ASSERT_NO_FAILURES(CheckFieldShape(the_struct->members[0], ExpectedField{}));
}

TEST(TypeshapeTests, recursive_struct_with_handles) {
  TestLibrary library(kPrologWithHandleDefinition + R"FIDL(
struct TheStruct {
  handle:VMO some_handle;
  TheStruct? opt_one_more;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto the_struct = library.LookupStruct("TheStruct");
  ASSERT_NOT_NULL(the_struct);
  ASSERT_NO_FAILURES(
      CheckTypeShape(the_struct, Expected{
                                     .inline_size = 16,
                                     .alignment = 8,
                                     .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                     .max_handles = std::numeric_limits<uint32_t>::max(),
                                     .depth = std::numeric_limits<uint32_t>::max(),
                                     .has_padding = true,
                                     .is_resource = true,
                                 }));
  ASSERT_EQ(the_struct->members.size(), 2);
  ASSERT_NO_FAILURES(CheckFieldShape(the_struct->members[0], ExpectedField{
                                                                 .padding = 4,
                                                             }));
  ASSERT_NO_FAILURES(CheckFieldShape(the_struct->members[1], ExpectedField{
                                                                 .offset = 8,
                                                             }));
}

TEST(TypeshapeTests, co_recursive_struct) {
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
  ASSERT_NOT_NULL(struct_a);
  ASSERT_NO_FAILURES(
      CheckTypeShape(struct_a, Expected{
                                   .inline_size = 8,
                                   .alignment = 8,
                                   .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                   .max_handles = 0,
                                   .depth = std::numeric_limits<uint32_t>::max(),
                               }));

  auto struct_b = library.LookupStruct("B");
  ASSERT_NOT_NULL(struct_b);
  ASSERT_NO_FAILURES(
      CheckTypeShape(struct_b, Expected{
                                   .inline_size = 8,
                                   .alignment = 8,
                                   .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                   .max_handles = 0,
                                   .depth = std::numeric_limits<uint32_t>::max(),
                               }));
}

TEST(TypeshapeTests, co_recursive_struct_with_handles) {
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
  ASSERT_NOT_NULL(struct_a);
  ASSERT_NO_FAILURES(
      CheckTypeShape(struct_a, Expected{
                                   .inline_size = 16,
                                   .alignment = 8,
                                   .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                   .max_handles = std::numeric_limits<uint32_t>::max(),
                                   .depth = std::numeric_limits<uint32_t>::max(),
                                   .has_padding = true,
                                   .is_resource = true,
                               }));

  auto struct_b = library.LookupStruct("B");
  ASSERT_NOT_NULL(struct_b);
  ASSERT_NO_FAILURES(
      CheckTypeShape(struct_b, Expected{
                                   .inline_size = 16,
                                   .alignment = 8,
                                   .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                   .max_handles = std::numeric_limits<uint32_t>::max(),
                                   .depth = std::numeric_limits<uint32_t>::max(),
                                   .has_padding = true,
                                   .is_resource = true,
                               }));
}

TEST(TypeshapeTests, co_recursive_struct2) {
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
  ASSERT_NOT_NULL(struct_foo);
  ASSERT_NO_FAILURES(
      CheckTypeShape(struct_foo, Expected{
                                     .inline_size = 8,
                                     .alignment = 8,
                                     .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                     .max_handles = 0,
                                     .depth = std::numeric_limits<uint32_t>::max(),
                                 }));

  auto struct_bar = library.LookupStruct("Bar");
  ASSERT_NOT_NULL(struct_bar);
  ASSERT_NO_FAILURES(
      CheckTypeShape(struct_bar, Expected{
                                     .inline_size = 8,
                                     .alignment = 8,
                                     .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                     .max_handles = 0,
                                     .depth = std::numeric_limits<uint32_t>::max(),
                                 }));
}

TEST(TypeshapeTests, struct_two_deep) {
  TestLibrary library(kPrologWithHandleDefinition + R"FIDL(
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
    handle:VMO vmo;
    uint64 size;
};

enum Priority {
    EAGER = 0;
    LAZY = 1;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto buffer = library.LookupStruct("Buffer");
  ASSERT_NOT_NULL(buffer);
  ASSERT_NO_FAILURES(CheckTypeShape(buffer, Expected{
                                                .inline_size = 16,
                                                .alignment = 8,
                                                .max_handles = 1,
                                                .has_padding = true,
                                                .is_resource = true,
                                            }));

  auto value = library.LookupStruct("Value");
  ASSERT_NOT_NULL(value);
  ASSERT_NO_FAILURES(CheckTypeShape(
      value, Expected{
                 .inline_size = 16,
                 .alignment = 8,
                 .max_out_of_line = 16,
                 .max_handles = 1,
                 .depth = 1,
                 .has_padding = true,  // because the size of |Priority| defaults to uint32
                 .is_resource = true,
             }));

  auto diff_entry = library.LookupStruct("DiffEntry");
  ASSERT_NOT_NULL(diff_entry);
  ASSERT_NO_FAILURES(
      CheckTypeShape(diff_entry, Expected{
                                     .inline_size = 40,
                                     .alignment = 8,
                                     .max_out_of_line = 352,
                                     .max_handles = 3,
                                     .depth = 2,
                                     .has_padding = true,  // because |Value| has padding
                                     .is_resource = true,
                                 }));
}

TEST(TypeshapeTests, protocol_child_and_parent) {
  SharedAmongstLibraries shared;
  TestLibrary parent_library("parent.fidl", R"FIDL(
library parent;

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
  ASSERT_NOT_NULL(child);
  ASSERT_EQ(child->all_methods.size(), 1);
  auto& sync_with_info = child->all_methods[0];
  auto sync_request = sync_with_info.method->maybe_request;
  ASSERT_NOT_NULL(sync_request);
  ASSERT_NO_FAILURES(CheckTypeShape(sync_request,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                    },
                                    // Note that this typeshape is not actually included
                                    // in the JSON IR since it corresponds to an empty
                                    // payload.
                                    Expected{
                                        .inline_size = 8,
                                        .alignment = 8,
                                    }));
}

TEST(TypeshapeTests, union_size8alignment4_sandwich) {
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
  ASSERT_NOT_NULL(sandwich);
  ASSERT_NO_FAILURES(CheckTypeShape(sandwich,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 4,
                                        .max_handles = 0,
                                        .has_padding = false,
                                    },
                                    Expected{
                                        .inline_size = 40,
                                        .alignment = 8,
                                        .max_out_of_line = 8,
                                        .max_handles = 0,
                                        .depth = 1,
                                        .has_padding = true,
                                    }));
  ASSERT_EQ(sandwich->members.size(), 3);
  ASSERT_NO_FAILURES(CheckFieldShape(sandwich->members[0],  // before
                                     ExpectedField{
                                         .offset = 0,
                                         .padding = 0,
                                     },
                                     ExpectedField{
                                         .offset = 0,
                                         .padding = 4,
                                     }));
  ASSERT_NO_FAILURES(CheckFieldShape(sandwich->members[1],  // union
                                     ExpectedField{
                                         .offset = 4,
                                         .padding = 0,
                                     },
                                     ExpectedField{
                                         .offset = 8,
                                         .padding = 0,
                                     }));
  ASSERT_NO_FAILURES(CheckFieldShape(sandwich->members[2],  // after
                                     ExpectedField{
                                         .offset = 12,
                                         .padding = 0,
                                     },
                                     ExpectedField{
                                         .offset = 32,
                                         .padding = 4,
                                     }));
}

TEST(TypeshapeTests, union_size12alignment4_sandwich) {
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
  ASSERT_NOT_NULL(sandwich);
  ASSERT_NO_FAILURES(CheckTypeShape(sandwich,
                                    Expected{
                                        .inline_size = 20,
                                        .alignment = 4,
                                        .max_handles = 0,
                                        .has_padding = true,
                                    },
                                    Expected{
                                        .inline_size = 40,
                                        .alignment = 8,
                                        .max_out_of_line = 8,
                                        .max_handles = 0,
                                        .depth = 1,
                                        .has_padding = true,
                                    }));
  ASSERT_EQ(sandwich->members.size(), 3);
  ASSERT_NO_FAILURES(CheckFieldShape(sandwich->members[0],  // before
                                     ExpectedField{
                                         .offset = 0,
                                         .padding = 0,
                                     },
                                     ExpectedField{
                                         .offset = 0,
                                         .padding = 4,
                                     }));
  ASSERT_NO_FAILURES(CheckFieldShape(sandwich->members[1],  // union
                                     ExpectedField{
                                         .offset = 4,
                                         .padding = 0,
                                     },
                                     ExpectedField{
                                         .offset = 8,
                                         .padding = 0,
                                     }));
  ASSERT_NO_FAILURES(CheckFieldShape(sandwich->members[2],  // after
                                     ExpectedField{
                                         .offset = 16,
                                         .padding = 0,
                                     },
                                     ExpectedField{
                                         .offset = 32,
                                         .padding = 4,
                                     }));
}

TEST(TypeshapeTests, union_size24alignment8_sandwich) {
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
  ASSERT_NOT_NULL(sandwich);
  ASSERT_NO_FAILURES(CheckTypeShape(sandwich,
                                    Expected{
                                        .inline_size = 40,
                                        .alignment = 8,
                                        .max_handles = 0,
                                        .has_padding = true,
                                    },
                                    Expected{
                                        .inline_size = 40,
                                        .alignment = 8,
                                        .max_out_of_line = 16,
                                        .max_handles = 0,
                                        .depth = 1,
                                        .has_padding = true,
                                    }));
  ASSERT_EQ(sandwich->members.size(), 3);
  ASSERT_NO_FAILURES(CheckFieldShape(sandwich->members[0],  // before
                                     ExpectedField{
                                         .offset = 0,
                                         .padding = 4,
                                     },
                                     ExpectedField{
                                         .offset = 0,
                                         .padding = 4,
                                     }));
  ASSERT_NO_FAILURES(CheckFieldShape(sandwich->members[1],  // union
                                     ExpectedField{
                                         .offset = 8,
                                         .padding = 0,
                                     },
                                     ExpectedField{
                                         .offset = 8,
                                         .padding = 0,
                                     }));
  ASSERT_NO_FAILURES(CheckFieldShape(sandwich->members[2],  // after
                                     ExpectedField{
                                         .offset = 32,
                                         .padding = 4,
                                     },
                                     ExpectedField{
                                         .offset = 32,
                                         .padding = 4,
                                     }));
}

TEST(TypeshapeTests, union_size36alignment4_sandwich) {
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
  ASSERT_NOT_NULL(sandwich);
  ASSERT_NO_FAILURES(CheckTypeShape(sandwich,
                                    Expected{
                                        .inline_size = 44,
                                        .alignment = 4,
                                        .max_handles = 0,
                                        .has_padding = false,
                                    },
                                    Expected{
                                        .inline_size = 40,
                                        .alignment = 8,
                                        .max_out_of_line = 32,
                                        .max_handles = 0,
                                        .depth = 1,
                                        .has_padding = true,
                                    }));
  ASSERT_EQ(sandwich->members.size(), 3);
  ASSERT_NO_FAILURES(CheckFieldShape(sandwich->members[0],  // before
                                     ExpectedField{
                                         .offset = 0,
                                         .padding = 0,
                                     },
                                     ExpectedField{
                                         .offset = 0,
                                         .padding = 4,
                                     }));
  ASSERT_NO_FAILURES(CheckFieldShape(sandwich->members[1],  // union
                                     ExpectedField{
                                         .offset = 4,
                                         .padding = 0,
                                     },
                                     ExpectedField{
                                         .offset = 8,
                                         .padding = 0,
                                     }));
  ASSERT_NO_FAILURES(CheckFieldShape(sandwich->members[2],  // after
                                     ExpectedField{
                                         .offset = 40,
                                         .padding = 0,
                                     },
                                     ExpectedField{
                                         .offset = 32,
                                         .padding = 4,
                                     }));
}

TEST(TypeshapeTests, zero_size_vector) {
  TestLibrary library(R"FIDL(
library example;

struct A {
    vector<handle>:0 zero_size;
};

)FIDL");
  ASSERT_TRUE(library.Compile());

  auto struct_a = library.LookupStruct("A");
  ASSERT_NOT_NULL(struct_a);
  ASSERT_NO_FAILURES(CheckTypeShape(struct_a, Expected{
                                                  .inline_size = 16,
                                                  .alignment = 8,
                                                  .max_out_of_line = 0,
                                                  .max_handles = 0,
                                                  .depth = 1,
                                                  .has_padding = true,
                                                  .is_resource = true,
                                              }));
}

}  // namespace
