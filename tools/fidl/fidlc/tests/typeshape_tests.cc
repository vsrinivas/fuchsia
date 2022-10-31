// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/include/fidl/flat_ast.h"
#include "tools/fidl/fidlc/include/fidl/type_shape.h"
#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

namespace {

const std::string kPrologWithHandleDefinition(R"FIDL(
library example;

type obj_type = enum : uint32 {
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
        subtype obj_type;
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
  bool has_envelope = false;
  bool has_flexible_envelope = false;
};

void CheckTypeShape(const fidl::TypeShape& actual, Expected expected) {
  EXPECT_EQ(expected.inline_size, actual.inline_size);
  EXPECT_EQ(expected.alignment, actual.alignment);
  EXPECT_EQ(expected.max_out_of_line, actual.max_out_of_line);
  EXPECT_EQ(expected.max_handles, actual.max_handles);
  EXPECT_EQ(expected.depth, actual.depth);
  EXPECT_EQ(expected.has_padding, actual.has_padding);
  EXPECT_EQ(expected.has_envelope, actual.has_envelope);
  EXPECT_EQ(expected.has_flexible_envelope, actual.has_flexible_envelope);
}

void CheckTypeShape(const fidl::flat::Object* actual, Expected expected_v1_no_ee,
                    Expected expected_v1_header, Expected expected_v2,
                    Expected expected_v2_header) {
  ASSERT_NO_FAILURES(
      CheckTypeShape(fidl::TypeShape(actual, fidl::WireFormat::kV1NoEe), expected_v1_no_ee));
  ASSERT_NO_FAILURES(
      CheckTypeShape(fidl::TypeShape(actual, fidl::WireFormat::kV1NoEe), expected_v1_header));
  ASSERT_NO_FAILURES(CheckTypeShape(fidl::TypeShape(actual, fidl::WireFormat::kV2), expected_v2));
  ASSERT_NO_FAILURES(
      CheckTypeShape(fidl::TypeShape(actual, fidl::WireFormat::kV2), expected_v2_header));
}

void CheckTypeShape(const fidl::flat::Object* actual, Expected expected_v1_no_ee,
                    Expected expected_v2) {
  ASSERT_NO_FAILURES(
      CheckTypeShape(fidl::TypeShape(actual, fidl::WireFormat::kV1NoEe), expected_v1_no_ee));
  ASSERT_NO_FAILURES(CheckTypeShape(fidl::TypeShape(actual, fidl::WireFormat::kV2), expected_v2));
}

void CheckTypeShape(const fidl::flat::Object* actual, Expected expected) {
  CheckTypeShape(actual, expected, expected);
}

struct ExpectedField {
  uint32_t offset = 0;
  uint32_t padding = 0;
};

template <typename T>
void CheckFieldShape(const T& field, ExpectedField expected_v1, ExpectedField expected_v2) {
  const fidl::FieldShape& actual_v1 = fidl::FieldShape(field, fidl::WireFormat::kV1NoEe);
  EXPECT_EQ(expected_v1.offset, actual_v1.offset);
  EXPECT_EQ(expected_v1.padding, actual_v1.padding);
  const fidl::FieldShape& actual_v2 = fidl::FieldShape(field, fidl::WireFormat::kV2);
  EXPECT_EQ(expected_v2.offset, actual_v2.offset);
  EXPECT_EQ(expected_v2.padding, actual_v2.padding);
}

template <typename T>
void CheckFieldShape(const T& field, ExpectedField expected) {
  CheckFieldShape(field, expected, expected);
}

TEST(TypeshapeTests, GoodEmptyStruct) {
  TestLibrary test_library(R"FIDL(library example;

type Empty = struct {};
)FIDL");
  ASSERT_COMPILED(test_library);

  auto empty = test_library.LookupStruct("Empty");
  ASSERT_NOT_NULL(empty);
  ASSERT_NO_FAILURES(CheckTypeShape(empty, Expected{
                                               .inline_size = 1,
                                               .alignment = 1,
                                           }));
  ASSERT_EQ(empty->members.size(), 0);
}

TEST(TypeshapeTests, GoodEmptyStructWithinAnotherStruct) {
  TestLibrary test_library(R"FIDL(library example;

type Empty = struct {};

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
type EmptyWithOtherThings = struct {
    a bool;
    // no padding
    b Empty;
    // no padding
    c int16;
    // no padding
    d Empty;
    // 3 bytes padding
    e int32;
    // no padding
    f int16;
    // no padding
    g Empty;
    // no padding
    h Empty;
};
)FIDL");
  ASSERT_COMPILED(test_library);

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

TEST(TypeshapeTests, GoodSimpleNewTypes) {
  TestLibrary test_library(R"FIDL(library example;

type BoolAndU32 = struct {
    b bool;
    u uint32;
};
type NewBoolAndU32 = BoolAndU32;

type BitsImplicit = strict bits {
    VALUE = 1;
};
type NewBitsImplicit = BitsImplicit;


type TableWithBoolAndU32 = table {
    1: b bool;
    2: u uint32;
};
type NewTableWithBoolAndU32 = TableWithBoolAndU32;

type BoolAndU64 = struct {
    b bool;
    u uint64;
};
type UnionOfThings = strict union {
    1: ob bool;
    2: bu BoolAndU64;
};
type NewUnionOfThings = UnionOfThings;
)FIDL");
  test_library.EnableFlag(fidl::ExperimentalFlags::Flag::kAllowNewTypes);
  ASSERT_COMPILED(test_library);

  auto new_bool_and_u32_struct = test_library.LookupNewType("NewBoolAndU32");
  ASSERT_NOT_NULL(new_bool_and_u32_struct);
  ASSERT_NO_FAILURES(CheckTypeShape(new_bool_and_u32_struct, Expected{
                                                                 .inline_size = 8,
                                                                 .alignment = 4,
                                                                 .has_padding = true,
                                                             }));

  auto new_bits_implicit = test_library.LookupNewType("NewBitsImplicit");
  EXPECT_NOT_NULL(new_bits_implicit);
  ASSERT_NO_FAILURES(CheckTypeShape(new_bits_implicit, Expected{
                                                           .inline_size = 4,
                                                           .alignment = 4,
                                                       }));

  auto new_bool_and_u32_table = test_library.LookupNewType("NewTableWithBoolAndU32");
  ASSERT_NOT_NULL(new_bool_and_u32_table);
  ASSERT_NO_FAILURES(CheckTypeShape(new_bool_and_u32_table,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 48,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 16,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto new_union = test_library.LookupNewType("NewUnionOfThings");
  ASSERT_NOT_NULL(new_union);
  ASSERT_NO_FAILURES(CheckTypeShape(new_union,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 16,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 16,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_envelope = true,
                                    }));
}

TEST(TypeshapeTests, GoodSimpleStructs) {
  TestLibrary test_library(R"FIDL(library example;

type OneBool = struct {
    b bool;
};

type TwoBools = struct {
    a bool;
    b bool;
};

type BoolAndU32 = struct {
    b bool;
    u uint32;
};

type BoolAndU64 = struct {
    b bool;
    u uint64;
};
)FIDL");
  ASSERT_COMPILED(test_library);

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

TEST(TypeshapeTests, GoodSimpleStructsWithHandles) {
  TestLibrary test_library(kPrologWithHandleDefinition + R"FIDL(
type OneHandle = resource struct {
  h handle;
};

type TwoHandles = resource struct {
  h1 handle:CHANNEL;
  h2 handle:PORT;
};

type ThreeHandlesOneOptional = resource struct {
  h1 handle:CHANNEL;
  h2 handle:PORT;
  opt_h3 handle:<TIMER, optional>;
};
)FIDL");
  ASSERT_COMPILED(test_library);

  auto one_handle = test_library.LookupStruct("OneHandle");
  ASSERT_NOT_NULL(one_handle);
  ASSERT_NO_FAILURES(CheckTypeShape(one_handle, Expected{
                                                    .inline_size = 4,
                                                    .alignment = 4,
                                                    .max_handles = 1,
                                                }));
  ASSERT_EQ(one_handle->members.size(), 1);
  ASSERT_NO_FAILURES(CheckFieldShape(one_handle->members[0], ExpectedField{}));

  auto two_handles = test_library.LookupStruct("TwoHandles");
  ASSERT_NOT_NULL(two_handles);
  ASSERT_NO_FAILURES(CheckTypeShape(two_handles, Expected{
                                                     .inline_size = 8,
                                                     .alignment = 4,
                                                     .max_handles = 2,
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

TEST(TypeshapeTests, GoodBits) {
  TestLibrary test_library(R"FIDL(library example;

type Bits16 = strict bits : uint16 {
    VALUE = 1;
};

type BitsImplicit = strict bits {
    VALUE = 1;
};
)FIDL");
  ASSERT_COMPILED(test_library);

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

TEST(TypeshapeTests, GoodSimpleTables) {
  TestLibrary test_library(R"FIDL(library example;

type TableWithNoMembers = table {};

type TableWithOneBool = table {
    1: b bool;
};

type TableWithTwoBools = table {
    1: a bool;
    2: b bool;
};

type TableWithBoolAndU32 = table {
    1: b bool;
    2: u uint32;
};

type TableWithBoolAndU64 = table {
    1: b bool;
    2: u uint64;
};
)FIDL");
  ASSERT_COMPILED(test_library);

  auto no_members = test_library.LookupTable("TableWithNoMembers");
  ASSERT_NOT_NULL(no_members);
  ASSERT_NO_FAILURES(CheckTypeShape(no_members, Expected{
                                                    .inline_size = 16,
                                                    .alignment = 8,
                                                    .depth = 1,
                                                    .has_padding = false,
                                                    .has_envelope = true,
                                                    .has_flexible_envelope = true,
                                                }));

  auto one_bool = test_library.LookupTable("TableWithOneBool");
  ASSERT_NOT_NULL(one_bool);
  ASSERT_NO_FAILURES(CheckTypeShape(one_bool,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 24,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 8,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto two_bools = test_library.LookupTable("TableWithTwoBools");
  ASSERT_NOT_NULL(two_bools);
  ASSERT_NO_FAILURES(CheckTypeShape(two_bools,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 48,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 16,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto bool_and_u32 = test_library.LookupTable("TableWithBoolAndU32");
  ASSERT_NOT_NULL(bool_and_u32);
  ASSERT_NO_FAILURES(CheckTypeShape(bool_and_u32,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 48,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 16,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto bool_and_u64 = test_library.LookupTable("TableWithBoolAndU64");
  ASSERT_NOT_NULL(bool_and_u64);
  ASSERT_NO_FAILURES(CheckTypeShape(bool_and_u64,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 48,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 24,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));
}

TEST(TypeshapeTests, GoodTablesWithReservedFields) {
  TestLibrary test_library(R"FIDL(library example;

type SomeReserved = table {
    1: b bool;
    2: reserved;
    3: b2 bool;
    4: reserved;
};

type LastNonReserved = table {
    1: reserved;
    2: reserved;
    3: b bool;
};

type LastReserved = table {
    1: b bool;
    2: b2 bool;
    3: reserved;
    4: reserved;
};

type AllReserved = table {
    1: reserved;
    2: reserved;
    3: reserved;
};

type OneReserved = table {
    1: reserved;
};
)FIDL");
  ASSERT_COMPILED(test_library);

  auto some_reserved = test_library.LookupTable("SomeReserved");
  ASSERT_NOT_NULL(some_reserved);
  ASSERT_NO_FAILURES(CheckTypeShape(some_reserved,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 64,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 24,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto last_non_reserved = test_library.LookupTable("LastNonReserved");
  ASSERT_NOT_NULL(last_non_reserved);
  ASSERT_NO_FAILURES(CheckTypeShape(last_non_reserved,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 56,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 24,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto last_reserved = test_library.LookupTable("LastReserved");
  ASSERT_NOT_NULL(last_reserved);
  ASSERT_NO_FAILURES(CheckTypeShape(last_reserved,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 48,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 16,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
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
                                                      .has_envelope = true,
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
                                                      .has_envelope = true,
                                                      .has_flexible_envelope = true,
                                                  }));
}

TEST(TypeshapeTests, GoodSimpleTablesWithHandles) {
  TestLibrary test_library(R"FIDL(
library example;
using zx;

type TableWithOneHandle = resource table {
  1: h zx.handle;
};

)FIDL");
  test_library.UseLibraryZx();
  ASSERT_COMPILED(test_library);

  auto one_handle = test_library.LookupTable("TableWithOneHandle");
  ASSERT_NOT_NULL(one_handle);
  ASSERT_NO_FAILURES(CheckTypeShape(one_handle,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 24,
                                        .max_handles = 1,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 8,
                                        .max_handles = 1,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));
}

TEST(TypeshapeTests, GoodOptionalStructs) {
  TestLibrary test_library(R"FIDL(library example;

type OneBool = struct {
    b bool;
};

type OptionalOneBool = struct {
    s box<OneBool>;
};

type TwoBools = struct {
    a bool;
    b bool;
};

type OptionalTwoBools = struct {
    s box<TwoBools>;
};

type BoolAndU32 = struct {
    b bool;
    u uint32;
};

type OptionalBoolAndU32 = struct {
    s box<BoolAndU32>;
};

type BoolAndU64 = struct {
    b bool;
    u uint64;
};

type OptionalBoolAndU64 = struct {
    s box<BoolAndU64>;
};
)FIDL");
  ASSERT_COMPILED(test_library);

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

TEST(TypeshapeTests, GoodOptionalTables) {
  TestLibrary test_library(R"FIDL(library example;

type OneBool = struct {
    b bool;
};

type TableWithOptionalOneBool = table {
    1: s OneBool;
};

type TableWithOneBool = table {
    1: b bool;
};

type TableWithOptionalTableWithOneBool = table {
    1: s TableWithOneBool;
};

type TwoBools = struct {
    a bool;
    b bool;
};

type TableWithOptionalTwoBools = table {
    1: s TwoBools;
};

type TableWithTwoBools = table {
    1: a bool;
    2: b bool;
};

type TableWithOptionalTableWithTwoBools = table {
    1: s TableWithTwoBools;
};

type BoolAndU32 = struct {
    b bool;
    u uint32;
};

type TableWithOptionalBoolAndU32 = table {
    1: s BoolAndU32;
};

type TableWithBoolAndU32 = table {
    1: b bool;
    2: u uint32;
};

type TableWithOptionalTableWithBoolAndU32 = table {
    1: s TableWithBoolAndU32;
};

type BoolAndU64 = struct {
    b bool;
    u uint64;
};

type TableWithOptionalBoolAndU64 = table {
    1: s BoolAndU64;
};

type TableWithBoolAndU64 = table {
    1: b bool;
    2: u uint64;
};

type TableWithOptionalTableWithBoolAndU64 = table {
    1: s TableWithBoolAndU64;
};
)FIDL");
  ASSERT_COMPILED(test_library);

  auto one_bool = test_library.LookupTable("TableWithOptionalOneBool");
  ASSERT_NOT_NULL(one_bool);
  ASSERT_NO_FAILURES(CheckTypeShape(one_bool,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 24,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 8,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto table_with_one_bool = test_library.LookupTable("TableWithOptionalTableWithOneBool");
  ASSERT_NOT_NULL(table_with_one_bool);
  ASSERT_NO_FAILURES(CheckTypeShape(table_with_one_bool,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 56,
                                        .depth = 4,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 32,
                                        .depth = 4,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto two_bools = test_library.LookupTable("TableWithOptionalTwoBools");
  ASSERT_NOT_NULL(two_bools);
  ASSERT_NO_FAILURES(CheckTypeShape(two_bools,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 24,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 8,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto table_with_two_bools = test_library.LookupTable("TableWithOptionalTableWithTwoBools");
  ASSERT_NOT_NULL(table_with_two_bools);
  ASSERT_NO_FAILURES(CheckTypeShape(table_with_two_bools,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 80,
                                        .depth = 4,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 40,
                                        .depth = 4,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto bool_and_u32 = test_library.LookupTable("TableWithOptionalBoolAndU32");
  ASSERT_NOT_NULL(bool_and_u32);
  ASSERT_NO_FAILURES(CheckTypeShape(bool_and_u32,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 24,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 16,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto table_with_bool_and_u32 = test_library.LookupTable("TableWithOptionalTableWithBoolAndU32");
  ASSERT_NOT_NULL(table_with_bool_and_u32);
  ASSERT_NO_FAILURES(CheckTypeShape(table_with_bool_and_u32,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 80,
                                        .depth = 4,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 40,
                                        .depth = 4,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto bool_and_u64 = test_library.LookupTable("TableWithOptionalBoolAndU64");
  ASSERT_NOT_NULL(bool_and_u64);
  ASSERT_NO_FAILURES(CheckTypeShape(bool_and_u64,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 32,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 24,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto table_with_bool_and_u64 = test_library.LookupTable("TableWithOptionalTableWithBoolAndU64");
  ASSERT_NOT_NULL(table_with_bool_and_u64);
  ASSERT_NO_FAILURES(CheckTypeShape(table_with_bool_and_u64,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 80,
                                        .depth = 4,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 48,
                                        .depth = 4,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));
}

TEST(TypeshapeTests, GoodUnions) {
  TestLibrary test_library(R"FIDL(library example;

type BoolAndU64 = struct {
    b bool;
    u uint64;
};

type UnionOfThings = strict union {
    1: ob bool;
    2: bu BoolAndU64;
};

type Bool = struct {
    b bool;
};

type OptBool = struct {
    opt_b box<Bool>;
};

type UnionWithOutOfLine = strict union {
    1: opt_bool OptBool;
};

type OptionalUnion = struct {
    u UnionOfThings:optional;
};

type TableWithOptionalUnion = table {
    1: u UnionOfThings;
};
)FIDL");
  ASSERT_COMPILED(test_library);

  auto union_with_out_of_line = test_library.LookupUnion("UnionWithOutOfLine");
  ASSERT_NO_FAILURES(CheckTypeShape(union_with_out_of_line,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 16,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 16,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                    }));

  auto a_union = test_library.LookupUnion("UnionOfThings");
  ASSERT_NOT_NULL(a_union);
  ASSERT_NO_FAILURES(CheckTypeShape(a_union,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 16,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 16,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_envelope = true,
                                    }));
  ASSERT_EQ(a_union->members.size(), 2);
  ASSERT_NOT_NULL(a_union->members[0].maybe_used);
  ASSERT_NO_FAILURES(CheckFieldShape(*a_union->members[0].maybe_used,
                                     ExpectedField{
                                         .offset = 0,
                                         .padding = 7,
                                     },
                                     ExpectedField{
                                         .offset = 0,
                                         .padding = 7,
                                     }));
  ASSERT_NOT_NULL(a_union->members[1].maybe_used);
  ASSERT_NO_FAILURES(
      CheckFieldShape(*a_union->members[1].maybe_used, ExpectedField{}, ExpectedField{}));

  auto optional_union = test_library.LookupStruct("OptionalUnion");
  ASSERT_NOT_NULL(optional_union);
  ASSERT_NO_FAILURES(CheckTypeShape(optional_union,
                                    Expected{
                                        // because |UnionOfThings| union header is inline
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 16,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_envelope = true,
                                    },
                                    Expected{
                                        // because |UnionOfThings| union header is inline
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 16,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_envelope = true,
                                    }));

  auto table_with_optional_union = test_library.LookupTable("TableWithOptionalUnion");
  ASSERT_NOT_NULL(table_with_optional_union);
  ASSERT_NO_FAILURES(CheckTypeShape(table_with_optional_union,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 56,
                                        .depth = 3,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 40,
                                        .depth = 3,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));
}

TEST(TypeshapeTests, GoodUnionsWithHandles) {
  TestLibrary test_library(R"FIDL(
library example;
using zx;

type OneHandleUnion = strict resource union {
  1: one_handle zx.handle;
  2: one_bool bool;
  3: one_int uint32;
};

type ManyHandleUnion = strict resource union {
  1: one_handle zx.handle;
  2: handle_array array<zx.handle, 8>;
  3: handle_vector vector<zx.handle>:8;
};

)FIDL");
  test_library.UseLibraryZx();
  ASSERT_COMPILED(test_library);

  auto one_handle_union = test_library.LookupUnion("OneHandleUnion");
  ASSERT_NOT_NULL(one_handle_union);
  ASSERT_NO_FAILURES(CheckTypeShape(one_handle_union,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 8,
                                        .max_handles = 1,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 0,
                                        .max_handles = 1,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_envelope = true,
                                    }));
  ASSERT_EQ(one_handle_union->members.size(), 3);
  ASSERT_NOT_NULL(one_handle_union->members[0].maybe_used);
  ASSERT_NO_FAILURES(CheckFieldShape(*one_handle_union->members[0].maybe_used,
                                     ExpectedField{
                                         .offset = 0,
                                         .padding = 4,
                                     },
                                     ExpectedField{
                                         .offset = 0,
                                         .padding = 4,
                                     }));
  ASSERT_NOT_NULL(one_handle_union->members[1].maybe_used);
  ASSERT_NO_FAILURES(CheckFieldShape(*one_handle_union->members[1].maybe_used,
                                     ExpectedField{
                                         .offset = 0,
                                         .padding = 7,
                                     },
                                     ExpectedField{
                                         .offset = 0,
                                         .padding = 7,
                                     }));
  ASSERT_NOT_NULL(one_handle_union->members[2].maybe_used);
  ASSERT_NO_FAILURES(CheckFieldShape(*one_handle_union->members[2].maybe_used,
                                     ExpectedField{
                                         .offset = 0,
                                         .padding = 4,
                                     },
                                     ExpectedField{
                                         .offset = 0,
                                         .padding = 4,
                                     }));

  auto many_handle_union = test_library.LookupUnion("ManyHandleUnion");
  ASSERT_NOT_NULL(many_handle_union);
  ASSERT_NO_FAILURES(CheckTypeShape(many_handle_union,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 48,
                                        .max_handles = 8,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 48,
                                        .max_handles = 8,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                    }));
  ASSERT_EQ(many_handle_union->members.size(), 3);
  ASSERT_NOT_NULL(many_handle_union->members[1].maybe_used);
  ASSERT_NO_FAILURES(CheckFieldShape(*many_handle_union->members[0].maybe_used,
                                     ExpectedField{
                                         .offset = 0,
                                         .padding = 4,
                                     },
                                     ExpectedField{
                                         .offset = 0,
                                         .padding = 4,
                                     }));
  ASSERT_NOT_NULL(many_handle_union->members[1].maybe_used);
  ASSERT_NO_FAILURES(
      CheckFieldShape(*many_handle_union->members[1].maybe_used, ExpectedField{}, ExpectedField{}));
  ASSERT_NOT_NULL(many_handle_union->members[2].maybe_used);
  ASSERT_NO_FAILURES(
      CheckFieldShape(*many_handle_union->members[2].maybe_used, ExpectedField{}, ExpectedField{}));
}

TEST(TypeshapeTests, GoodVectors) {
  TestLibrary test_library(R"FIDL(library example;

type PaddedVector = struct {
    pv vector<int32>:3;
};

type NoPaddingVector = struct {
    npv vector<uint64>:3;
};

type UnboundedVector = struct {
    uv vector<int32>;
};

type UnboundedVectors = struct {
    uv1 vector<int32>;
    uv2 vector<int32>;
};

type TableWithPaddedVector = table {
    1: pv vector<int32>:3;
};

type TableWithUnboundedVector = table {
    1: uv vector<int32>;
};

type TableWithUnboundedVectors = table {
    1: uv1 vector<int32>;
    2: uv2 vector<int32>;
};
)FIDL");
  ASSERT_COMPILED(test_library);

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
  ASSERT_NO_FAILURES(CheckTypeShape(table_with_padded_vector,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 48,
                                        .depth = 3,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 40,
                                        .depth = 3,
                                        .has_padding = true,
                                        .has_envelope = true,
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
                                        .has_envelope = true,
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
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));
}

TEST(TypeshapeTests, GoodVectorsWithHandles) {
  TestLibrary test_library(R"FIDL(
library example;
using zx;

type HandleVector = resource struct {
  hv vector<zx.handle>:8;
};

type HandleNullableVector = resource struct {
  hv vector<zx.handle>:<8, optional>;
};

type TableWithHandleVector = resource table {
  1: hv vector<zx.handle>:8;
};

type UnboundedHandleVector = resource struct {
  hv vector<zx.handle>;
};

type TableWithUnboundedHandleVector = resource table {
  1: hv vector<zx.handle>;
};

type OneHandle = resource struct {
  h zx.handle;
};

type HandleStructVector = resource struct {
  sv vector<OneHandle>:8;
};

type TableWithOneHandle = resource table {
  1: h zx.handle;
};

type HandleTableVector = resource struct {
  sv vector<TableWithOneHandle>:8;
};

type TableWithHandleStructVector = resource table {
  1: sv vector<OneHandle>:8;
};

)FIDL");
  test_library.UseLibraryZx();
  ASSERT_COMPILED(test_library);

  auto handle_vector = test_library.LookupStruct("HandleVector");
  ASSERT_NOT_NULL(handle_vector);
  ASSERT_NO_FAILURES(CheckTypeShape(handle_vector, Expected{
                                                       .inline_size = 16,
                                                       .alignment = 8,
                                                       .max_out_of_line = 32,
                                                       .max_handles = 8,
                                                       .depth = 1,
                                                       .has_padding = true,
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
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
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
                                                          }));

  auto handle_table_vector = test_library.LookupStruct("HandleTableVector");
  ASSERT_NOT_NULL(handle_table_vector);
  ASSERT_NO_FAILURES(CheckTypeShape(handle_table_vector,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 320,
                                        .max_handles = 8,
                                        .depth = 3,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 192,
                                        .max_handles = 8,
                                        .depth = 3,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto table_with_handle_struct_vector = test_library.LookupTable("TableWithHandleStructVector");
  ASSERT_NOT_NULL(table_with_handle_struct_vector);
  ASSERT_NO_FAILURES(CheckTypeShape(table_with_handle_struct_vector,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 64,
                                        .max_handles = 8,
                                        .depth = 3,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 56,
                                        .max_handles = 8,
                                        .depth = 3,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));
}

TEST(TypeshapeTests, GoodStrings) {
  TestLibrary test_library(R"FIDL(library example;

type ShortString = struct {
    s string:5;
};

type UnboundedString = struct {
    s string;
};

type TableWithShortString = table {
    1: s string:5;
};

type TableWithUnboundedString = table {
    1: s string;
};
)FIDL");
  ASSERT_COMPILED(test_library);

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
  ASSERT_NO_FAILURES(CheckTypeShape(table_with_short_string,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 40,
                                        .depth = 3,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 32,
                                        .depth = 3,
                                        .has_padding = true,
                                        .has_envelope = true,
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
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));
}

TEST(TypeshapeTests, GoodArrays) {
  TestLibrary test_library(R"FIDL(library example;

type AnArray = struct {
    a array<int64, 5>;
};

type TableWithAnArray = table {
    1: a array<int64, 5>;
};

type TableWithAnInt32ArrayWithPadding = table {
    1: a array<int32, 3>;
};

type TableWithAnInt32ArrayNoPadding = table {
    1: a array<int32, 4>;
};
)FIDL");
  ASSERT_COMPILED(test_library);

  auto an_array = test_library.LookupStruct("AnArray");
  ASSERT_NOT_NULL(an_array);
  ASSERT_NO_FAILURES(CheckTypeShape(an_array, Expected{
                                                  .inline_size = 40,
                                                  .alignment = 8,
                                              }));

  auto table_with_an_array = test_library.LookupTable("TableWithAnArray");
  ASSERT_NOT_NULL(table_with_an_array);
  ASSERT_NO_FAILURES(CheckTypeShape(table_with_an_array,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 56,
                                        .depth = 2,
                                        .has_padding = false,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 48,
                                        .depth = 2,
                                        .has_padding = false,
                                        .has_envelope = true,
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
                         .has_envelope = true,
                         .has_flexible_envelope = true,
                     },
                     Expected{
                         .inline_size = 16,
                         .alignment = 8,
                         .max_out_of_line = 24,
                         .depth = 2,
                         .has_padding = true,
                         .has_envelope = true,
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
                         .has_envelope = true,
                         .has_flexible_envelope = true,
                     },
                     Expected{
                         .inline_size = 16,
                         .alignment = 8,
                         .max_out_of_line = 24,
                         .depth = 2,
                         .has_padding = false,
                         .has_envelope = true,
                         .has_flexible_envelope = true,
                     }));
}

TEST(TypeshapeTests, GoodArraysWithHandles) {
  TestLibrary test_library(R"FIDL(
library example;
using zx;

type HandleArray = resource struct {
  h1 array<zx.handle, 8>;
};

type TableWithHandleArray = resource table {
  1: ha array<zx.handle, 8>;
};

type NullableHandleArray = resource struct {
  ha array<zx.handle:optional, 8>;
};

type TableWithNullableHandleArray = resource table {
  1: ha array<zx.handle:optional, 8>;
};

)FIDL");
  test_library.UseLibraryZx();
  ASSERT_COMPILED(test_library);

  auto handle_array = test_library.LookupStruct("HandleArray");
  ASSERT_NOT_NULL(handle_array);
  ASSERT_NO_FAILURES(CheckTypeShape(handle_array, Expected{
                                                      .inline_size = 32,
                                                      .alignment = 4,
                                                      .max_handles = 8,
                                                  }));

  auto table_with_handle_array = test_library.LookupTable("TableWithHandleArray");
  ASSERT_NOT_NULL(table_with_handle_array);
  ASSERT_NO_FAILURES(CheckTypeShape(table_with_handle_array,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 48,
                                        .max_handles = 8,
                                        .depth = 2,
                                        .has_padding = false,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 40,
                                        .max_handles = 8,
                                        .depth = 2,
                                        .has_padding = false,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto nullable_handle_array = test_library.LookupStruct("NullableHandleArray");
  ASSERT_NOT_NULL(nullable_handle_array);
  ASSERT_NO_FAILURES(CheckTypeShape(nullable_handle_array, Expected{
                                                               .inline_size = 32,
                                                               .alignment = 4,
                                                               .max_handles = 8,
                                                           }));

  auto table_with_nullable_handle_array = test_library.LookupTable("TableWithNullableHandleArray");
  ASSERT_NOT_NULL(table_with_nullable_handle_array);
  ASSERT_NO_FAILURES(CheckTypeShape(table_with_nullable_handle_array,
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 48,
                                        .max_handles = 8,
                                        .depth = 2,
                                        .has_padding = false,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 40,
                                        .max_handles = 8,
                                        .depth = 2,
                                        .has_padding = false,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));
}

// TODO(pascallouis): write an "xunions_with_handles" test case.

TEST(TypeshapeTests, GoodFlexibleUnions) {
  TestLibrary test_library(R"FIDL(library example;

type XUnionWithOneBool = flexible union {
    1: b bool;
};

type StructWithOptionalXUnionWithOneBool = struct {
    opt_xunion_with_bool XUnionWithOneBool:optional;
};

type XUnionWithBoundedOutOfLineObject = flexible union {
    // smaller than |v| below, so will not be selected for max-out-of-line
    // calculation.
    1: b bool;

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
    2: v vector<vector<int32>:5>:6;
};

type XUnionWithUnboundedOutOfLineObject = flexible union {
    1: s string;
};

type XUnionWithoutPayloadPadding = flexible union {
    1: a array<uint64, 7>;
};

type PaddingCheck = flexible union {
    1: three array<uint8, 3>;
    2: five array<uint8, 5>;
};
)FIDL");
  ASSERT_COMPILED(test_library);

  auto one_bool = test_library.LookupUnion("XUnionWithOneBool");
  ASSERT_NOT_NULL(one_bool);
  ASSERT_NO_FAILURES(CheckTypeShape(one_bool,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 8,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 0,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));
  ASSERT_EQ(one_bool->members.size(), 1);
  ASSERT_NOT_NULL(one_bool->members[0].maybe_used);
  ASSERT_NO_FAILURES(CheckFieldShape(*one_bool->members[0].maybe_used, ExpectedField{.padding = 7},
                                     ExpectedField{.padding = 7}));

  auto opt_one_bool = test_library.LookupStruct("StructWithOptionalXUnionWithOneBool");
  ASSERT_NOT_NULL(opt_one_bool);
  ASSERT_NO_FAILURES(CheckTypeShape(opt_one_bool,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 8,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 0,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto xu = test_library.LookupUnion("XUnionWithBoundedOutOfLineObject");
  ASSERT_NOT_NULL(xu);
  ASSERT_NO_FAILURES(CheckTypeShape(xu,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 256,
                                        .depth = 3,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 256,
                                        .depth = 3,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto unbounded = test_library.LookupUnion("XUnionWithUnboundedOutOfLineObject");
  ASSERT_NOT_NULL(unbounded);
  ASSERT_NO_FAILURES(CheckTypeShape(unbounded,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = std::numeric_limits<uint32_t>::max(),
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto xu_no_payload_padding = test_library.LookupUnion("XUnionWithoutPayloadPadding");
  ASSERT_NOT_NULL(xu_no_payload_padding);
  ASSERT_NO_FAILURES(
      CheckTypeShape(xu_no_payload_padding,
                     Expected{
                         .inline_size = 24,
                         .alignment = 8,
                         .max_out_of_line = 56,
                         .depth = 1,
                         // xunion always have padding, because its ordinal is 32 bits.
                         // TODO(fxbug.dev/7970): increase the ordinal size to 64 bits, such that
                         // there is no padding.
                         .has_padding = true,
                         .has_envelope = true,
                         .has_flexible_envelope = true,
                     },
                     Expected{
                         .inline_size = 16,
                         .alignment = 8,
                         .max_out_of_line = 56,
                         .depth = 1,
                         // xunion always have padding, because its ordinal is 32 bits.
                         // TODO(fxbug.dev/7970): increase the ordinal size to 64 bits, such that
                         // there is no padding.
                         .has_padding = true,
                         .has_envelope = true,
                         .has_flexible_envelope = true,
                     }));

  auto padding_check = test_library.LookupUnion("PaddingCheck");
  ASSERT_NOT_NULL(padding_check);
  ASSERT_NO_FAILURES(CheckTypeShape(padding_check,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 8,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 8,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));
  ASSERT_EQ(padding_check->members.size(), 2);
  ASSERT_NOT_NULL(padding_check->members[0].maybe_used);
  ASSERT_NO_FAILURES(CheckFieldShape(*padding_check->members[0].maybe_used,
                                     ExpectedField{.padding = 5}, ExpectedField{.padding = 5}));
  ASSERT_NO_FAILURES(CheckFieldShape(*padding_check->members[1].maybe_used,
                                     ExpectedField{.padding = 3}, ExpectedField{.padding = 3}));
}

TEST(TypeshapeTests, GoodEnvelopeStrictness) {
  TestLibrary test_library(R"FIDL(library example;

type StrictLeafXUnion = strict union {
    1: a int64;
};

type FlexibleLeafXUnion = flexible union {
    1: a int64;
};

type FlexibleXUnionOfStrictXUnion = flexible union {
    1: xu StrictLeafXUnion;
};

type FlexibleXUnionOfFlexibleXUnion = flexible union {
    1: xu FlexibleLeafXUnion;
};

type StrictXUnionOfStrictXUnion = strict union {
    1: xu StrictLeafXUnion;
};

type StrictXUnionOfFlexibleXUnion = strict union {
    1: xu FlexibleLeafXUnion;
};

type FlexibleLeafTable = table {};

type StrictXUnionOfFlexibleTable = strict union {
    1: ft FlexibleLeafTable;
};
)FIDL");
  ASSERT_COMPILED(test_library);

  auto strict_xunion = test_library.LookupUnion("StrictLeafXUnion");
  ASSERT_NOT_NULL(strict_xunion);
  ASSERT_NO_FAILURES(CheckTypeShape(strict_xunion,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 8,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 8,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_envelope = true,
                                    }));

  auto flexible_xunion = test_library.LookupUnion("FlexibleLeafXUnion");
  ASSERT_NOT_NULL(flexible_xunion);
  ASSERT_NO_FAILURES(CheckTypeShape(flexible_xunion,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 8,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 8,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto flexible_of_strict = test_library.LookupUnion("FlexibleXUnionOfStrictXUnion");
  ASSERT_NOT_NULL(flexible_of_strict);
  ASSERT_NO_FAILURES(CheckTypeShape(flexible_of_strict,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 32,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 24,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto flexible_of_flexible = test_library.LookupUnion("FlexibleXUnionOfFlexibleXUnion");
  ASSERT_NOT_NULL(flexible_of_flexible);
  ASSERT_NO_FAILURES(CheckTypeShape(flexible_of_flexible,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 32,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 24,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));

  auto strict_of_strict = test_library.LookupUnion("StrictXUnionOfStrictXUnion");
  ASSERT_NOT_NULL(strict_of_strict);
  ASSERT_NO_FAILURES(CheckTypeShape(strict_of_strict,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 32,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 24,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                    }));

  auto strict_of_flexible = test_library.LookupUnion("StrictXUnionOfFlexibleXUnion");
  ASSERT_NOT_NULL(strict_of_flexible);
  ASSERT_NO_FAILURES(CheckTypeShape(strict_of_flexible,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 32,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 24,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
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
                                                        .has_envelope = true,
                                                        .has_flexible_envelope = true,
                                                    }));

  auto strict_xunion_of_flexible_table = test_library.LookupUnion("StrictXUnionOfFlexibleTable");
  ASSERT_NOT_NULL(strict_xunion_of_flexible_table);
  ASSERT_NO_FAILURES(CheckTypeShape(strict_xunion_of_flexible_table,
                                    Expected{
                                        .inline_size = 24,
                                        .alignment = 8,
                                        .max_out_of_line = 16,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 16,
                                        .alignment = 8,
                                        .max_out_of_line = 16,
                                        .depth = 2,
                                        .has_padding = true,
                                        .has_envelope = true,
                                        .has_flexible_envelope = true,
                                    }));
}

TEST(TypeshapeTests, GoodProtocolsAndRequestOfProtocols) {
  TestLibrary test_library(R"FIDL(library example;

protocol SomeProtocol {};

type UsingSomeProtocol = resource struct {
    value client_end:SomeProtocol;
};

type UsingOptSomeProtocol = resource struct {
    value client_end:<SomeProtocol, optional>;
};

type UsingRequestSomeProtocol = resource struct {
    value server_end:SomeProtocol;
};

type UsingOptRequestSomeProtocol = resource struct {
    value server_end:<SomeProtocol, optional>;
};
)FIDL");
  ASSERT_COMPILED(test_library);

  auto using_some_protocol = test_library.LookupStruct("UsingSomeProtocol");
  ASSERT_NOT_NULL(using_some_protocol);
  ASSERT_NO_FAILURES(CheckTypeShape(using_some_protocol, Expected{
                                                             .inline_size = 4,
                                                             .alignment = 4,
                                                             .max_handles = 1,
                                                         }));

  auto using_opt_some_protocol = test_library.LookupStruct("UsingOptSomeProtocol");
  ASSERT_NOT_NULL(using_opt_some_protocol);
  ASSERT_NO_FAILURES(CheckTypeShape(using_opt_some_protocol, Expected{
                                                                 .inline_size = 4,
                                                                 .alignment = 4,
                                                                 .max_handles = 1,
                                                             }));

  auto using_request_some_protocol = test_library.LookupStruct("UsingRequestSomeProtocol");
  ASSERT_NOT_NULL(using_request_some_protocol);
  ASSERT_NO_FAILURES(CheckTypeShape(using_request_some_protocol, Expected{
                                                                     .inline_size = 4,
                                                                     .alignment = 4,
                                                                     .max_handles = 1,
                                                                 }));

  auto using_opt_request_some_protocol = test_library.LookupStruct("UsingOptRequestSomeProtocol");
  ASSERT_NOT_NULL(using_opt_request_some_protocol);
  ASSERT_NO_FAILURES(CheckTypeShape(using_opt_request_some_protocol, Expected{
                                                                         .inline_size = 4,
                                                                         .alignment = 4,
                                                                         .max_handles = 1,
                                                                     }));
}

TEST(TypeshapeTests, GoodExternalDefinitions) {
  TestLibrary test_library;
  test_library.UseLibraryZx();
  test_library.AddSource("example.fidl", R"FIDL(
library example;

using zx;

type ExternalArrayStruct = struct {
    a array<ExternalSimpleStruct, EXTERNAL_SIZE_DEF>;
};

type ExternalStringSizeStruct = struct {
    a string:EXTERNAL_SIZE_DEF;
};

type ExternalVectorSizeStruct = resource struct {
    a vector<zx.handle>:EXTERNAL_SIZE_DEF;
};

)FIDL");
  test_library.AddSource("extern_defs.fidl", R"FIDL(
library example;

const EXTERNAL_SIZE_DEF uint32 = ANOTHER_INDIRECTION;
const ANOTHER_INDIRECTION uint32 = 32;

type ExternalSimpleStruct = struct {
    a uint32;
};
)FIDL");
  ASSERT_COMPILED(test_library);

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
                                                    }));
}

TEST(TypeshapeTests, GoodSimpleRequest) {
  TestLibrary library(R"FIDL(library example;

protocol Test {
    Method(struct { a int16; b int16; });
};
)FIDL");
  ASSERT_COMPILED(library);

  auto protocol = library.LookupProtocol("Test");
  ASSERT_NOT_NULL(protocol);
  ASSERT_EQ(protocol->methods.size(), 1);
  auto& method = protocol->methods[0];
  auto method_request = method.maybe_request.get();
  EXPECT_EQ(method.has_request, true);
  ASSERT_NOT_NULL(method_request);

  auto id = static_cast<const fidl::flat::IdentifierType*>(method_request->type);
  auto as_struct = static_cast<const fidl::flat::Struct*>(id->type_decl);
  EXPECT_NOT_NULL(as_struct);

  ASSERT_NO_FAILURES(CheckTypeShape(as_struct,
                                    Expected{
                                        .inline_size = 4,
                                        .alignment = 2,
                                        .max_handles = 0,
                                        .has_padding = false,
                                    },
                                    Expected{
                                        .inline_size = 4,
                                        .alignment = 2,
                                        .max_handles = 0,
                                        .has_padding = false,
                                    },
                                    Expected{
                                        .inline_size = 4,
                                        .alignment = 2,
                                        .max_handles = 0,
                                        .has_padding = false,
                                    },
                                    Expected{
                                        .inline_size = 4,
                                        .alignment = 2,
                                        .max_handles = 0,
                                        .has_padding = false,
                                    }));

  ASSERT_EQ(as_struct->members.size(), 2);
  ASSERT_NO_FAILURES(
      CheckFieldShape(as_struct->members[0], ExpectedField{.offset = 0, .padding = 0}));
  ASSERT_NO_FAILURES(
      CheckFieldShape(as_struct->members[1], ExpectedField{.offset = 2, .padding = 0}));
}

TEST(TypeshapeTests, GoodSimpleResponse) {
  TestLibrary library(R"FIDL(library example;

protocol Test {
    Method() -> (struct { a int16; b int16; });
};
)FIDL");
  ASSERT_COMPILED(library);

  auto protocol = library.LookupProtocol("Test");
  ASSERT_NOT_NULL(protocol);
  ASSERT_EQ(protocol->methods.size(), 1);
  auto& method = protocol->methods[0];
  auto method_response = method.maybe_response.get();
  EXPECT_EQ(method.has_response, true);
  ASSERT_NOT_NULL(method_response);

  auto id = static_cast<const fidl::flat::IdentifierType*>(method_response->type);
  auto as_struct = static_cast<const fidl::flat::Struct*>(id->type_decl);
  EXPECT_NOT_NULL(as_struct);

  ASSERT_NO_FAILURES(CheckTypeShape(as_struct,
                                    Expected{
                                        .inline_size = 4,
                                        .alignment = 2,
                                        .max_handles = 0,
                                        .has_padding = false,
                                    },
                                    Expected{
                                        .inline_size = 4,
                                        .alignment = 2,
                                        .max_handles = 0,
                                        .has_padding = false,
                                    },
                                    Expected{
                                        .inline_size = 4,
                                        .alignment = 2,
                                        .max_handles = 0,
                                        .has_padding = false,
                                    },
                                    Expected{
                                        .inline_size = 4,
                                        .alignment = 2,
                                        .max_handles = 0,
                                        .has_padding = false,
                                    }));

  ASSERT_EQ(as_struct->members.size(), 2);
  ASSERT_NO_FAILURES(
      CheckFieldShape(as_struct->members[0], ExpectedField{.offset = 0, .padding = 0}));
  ASSERT_NO_FAILURES(
      CheckFieldShape(as_struct->members[1], ExpectedField{.offset = 2, .padding = 0}));
}

TEST(TypeshapeTests, GoodRecursiveRequest) {
  TestLibrary library(R"FIDL(library example;

type WebMessage = resource struct {
    message_port_req server_end:MessagePort;
};

protocol MessagePort {
    PostMessage(resource struct {
        message WebMessage;
    }) -> (struct {
        success bool;
    });
};
)FIDL");
  ASSERT_COMPILED(library);

  auto web_message = library.LookupStruct("WebMessage");
  ASSERT_NOT_NULL(web_message);
  ASSERT_NO_FAILURES(CheckTypeShape(web_message, Expected{
                                                     .inline_size = 4,
                                                     .alignment = 4,
                                                     .max_handles = 1,
                                                 }));
  ASSERT_EQ(web_message->members.size(), 1);
  ASSERT_NO_FAILURES(CheckFieldShape(web_message->members[0], ExpectedField{}));

  auto message_port = library.LookupProtocol("MessagePort");
  ASSERT_NOT_NULL(message_port);
  ASSERT_EQ(message_port->methods.size(), 1);
  auto& post_message = message_port->methods[0];
  auto post_message_request = post_message.maybe_request.get();
  EXPECT_EQ(post_message.has_request, true);
  ASSERT_NOT_NULL(post_message_request);

  auto id = static_cast<const fidl::flat::IdentifierType*>(post_message_request->type);
  auto as_struct = static_cast<const fidl::flat::Struct*>(id->type_decl);
  EXPECT_NOT_NULL(as_struct);

  ASSERT_NO_FAILURES(CheckTypeShape(as_struct,
                                    Expected{
                                        .inline_size = 4,
                                        .alignment = 4,
                                        .max_handles = 1,
                                        .has_padding = false,
                                    },
                                    Expected{
                                        .inline_size = 4,
                                        .alignment = 4,
                                        .max_handles = 1,
                                        .has_padding = false,
                                    },
                                    Expected{
                                        .inline_size = 4,
                                        .alignment = 4,
                                        .max_handles = 1,
                                        .has_padding = false,
                                    },
                                    Expected{
                                        .inline_size = 4,
                                        .alignment = 4,
                                        .max_handles = 1,
                                        .has_padding = false,
                                    }));
  ASSERT_EQ(as_struct->members.size(), 1);
  ASSERT_NO_FAILURES(
      CheckFieldShape(as_struct->members[0], ExpectedField{.offset = 0, .padding = 0}));
}

TEST(TypeshapeTests, GoodRecursiveOptRequest) {
  TestLibrary library(R"FIDL(library example;

type WebMessage = resource struct {
    opt_message_port_req server_end:<MessagePort, optional>;
};

protocol MessagePort {
    PostMessage(resource struct {
        message WebMessage;
    }) -> (struct {
        success bool;
    });
};
)FIDL");
  ASSERT_COMPILED(library);

  auto web_message = library.LookupStruct("WebMessage");
  ASSERT_NOT_NULL(web_message);
  ASSERT_NO_FAILURES(CheckTypeShape(web_message, Expected{
                                                     .inline_size = 4,
                                                     .alignment = 4,
                                                     .max_handles = 1,
                                                 }));

  auto message_port = library.LookupProtocol("MessagePort");
  ASSERT_NOT_NULL(message_port);
  ASSERT_EQ(message_port->methods.size(), 1);
  auto& post_message = message_port->methods[0];
  auto post_message_request = post_message.maybe_request.get();
  EXPECT_EQ(post_message.has_request, true);

  auto id = static_cast<const fidl::flat::IdentifierType*>(post_message_request->type);
  auto as_struct = static_cast<const fidl::flat::Struct*>(id->type_decl);
  EXPECT_NOT_NULL(as_struct);

  ASSERT_NO_FAILURES(CheckTypeShape(as_struct,
                                    Expected{
                                        .inline_size = 4,
                                        .alignment = 4,
                                        .max_handles = 1,
                                        .has_padding = false,
                                    },
                                    Expected{
                                        .inline_size = 4,
                                        .alignment = 4,
                                        .max_handles = 1,
                                        .has_padding = false,
                                    },
                                    Expected{
                                        .inline_size = 4,
                                        .alignment = 4,
                                        .max_handles = 1,
                                        .has_padding = false,
                                    },
                                    Expected{
                                        .inline_size = 4,
                                        .alignment = 4,
                                        .max_handles = 1,
                                        .has_padding = false,
                                    }));
  ASSERT_EQ(as_struct->members.size(), 1);
  ASSERT_NO_FAILURES(
      CheckFieldShape(as_struct->members[0], ExpectedField{.offset = 0, .padding = 0}));
}

TEST(TypeshapeTests, GoodRecursiveProtocol) {
  TestLibrary library(R"FIDL(library example;

type WebMessage = resource struct {
    message_port client_end:MessagePort;
};

protocol MessagePort {
    PostMessage(resource struct {
        message WebMessage;
    }) -> (struct {
        success bool;
    });
};
)FIDL");
  ASSERT_COMPILED(library);

  auto web_message = library.LookupStruct("WebMessage");
  ASSERT_NOT_NULL(web_message);
  ASSERT_NO_FAILURES(CheckTypeShape(web_message, Expected{
                                                     .inline_size = 4,
                                                     .alignment = 4,
                                                     .max_handles = 1,
                                                 }));

  auto message_port = library.LookupProtocol("MessagePort");
  ASSERT_NOT_NULL(message_port);
  ASSERT_EQ(message_port->methods.size(), 1);
  auto& post_message = message_port->methods[0];
  auto post_message_request = post_message.maybe_request.get();
  EXPECT_EQ(post_message.has_request, true);

  auto id = static_cast<const fidl::flat::IdentifierType*>(post_message_request->type);
  auto as_struct = static_cast<const fidl::flat::Struct*>(id->type_decl);
  EXPECT_NOT_NULL(as_struct);

  ASSERT_NO_FAILURES(CheckTypeShape(as_struct,
                                    Expected{
                                        .inline_size = 4,
                                        .alignment = 4,
                                        .max_handles = 1,
                                        .has_padding = false,
                                    },
                                    Expected{
                                        .inline_size = 4,
                                        .alignment = 4,
                                        .max_handles = 1,
                                        .has_padding = false,
                                    },
                                    Expected{
                                        .inline_size = 4,
                                        .alignment = 4,
                                        .max_handles = 1,
                                        .has_padding = false,
                                    },
                                    Expected{
                                        .inline_size = 4,
                                        .alignment = 4,
                                        .max_handles = 1,
                                        .has_padding = false,
                                    }));
  ASSERT_EQ(as_struct->members.size(), 1);
  ASSERT_NO_FAILURES(
      CheckFieldShape(as_struct->members[0], ExpectedField{.offset = 0, .padding = 0}));
}

TEST(TypeshapeTests, GoodRecursiveOptProtocol) {
  TestLibrary library(R"FIDL(library example;

type WebMessage = resource struct {
    opt_message_port client_end:<MessagePort, optional>;
};

protocol MessagePort {
    PostMessage(resource struct {
        message WebMessage;
    }) -> (struct {
        success bool;
    });
};
)FIDL");
  ASSERT_COMPILED(library);

  auto web_message = library.LookupStruct("WebMessage");
  ASSERT_NOT_NULL(web_message);
  ASSERT_NO_FAILURES(CheckTypeShape(web_message, Expected{
                                                     .inline_size = 4,
                                                     .alignment = 4,
                                                     .max_handles = 1,
                                                 }));

  auto message_port = library.LookupProtocol("MessagePort");
  ASSERT_NOT_NULL(message_port);
  ASSERT_EQ(message_port->methods.size(), 1);
  auto& post_message = message_port->methods[0];
  auto post_message_request = post_message.maybe_request.get();
  EXPECT_EQ(post_message.has_request, true);

  auto id = static_cast<const fidl::flat::IdentifierType*>(post_message_request->type);
  auto as_struct = static_cast<const fidl::flat::Struct*>(id->type_decl);
  EXPECT_NOT_NULL(as_struct);

  ASSERT_NO_FAILURES(CheckTypeShape(as_struct,
                                    Expected{
                                        .inline_size = 4,
                                        .alignment = 4,
                                        .max_handles = 1,
                                        .has_padding = false,
                                    },
                                    Expected{
                                        .inline_size = 4,
                                        .alignment = 4,
                                        .max_handles = 1,
                                        .has_padding = false,
                                    },
                                    Expected{
                                        .inline_size = 4,
                                        .alignment = 4,
                                        .max_handles = 1,
                                        .has_padding = false,
                                    },
                                    Expected{
                                        .inline_size = 4,
                                        .alignment = 4,
                                        .max_handles = 1,
                                        .has_padding = false,
                                    }));
  ASSERT_EQ(as_struct->members.size(), 1);
  ASSERT_NO_FAILURES(
      CheckFieldShape(as_struct->members[0], ExpectedField{.offset = 0, .padding = 0}));
}

TEST(TypeshapeTests, GoodRecursiveStruct) {
  TestLibrary library(R"FIDL(library example;

type TheStruct = struct {
    opt_one_more box<TheStruct>;
};
)FIDL");
  ASSERT_COMPILED(library);

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

TEST(TypeshapeTests, GoodRecursiveStructWithHandles) {
  TestLibrary library(kPrologWithHandleDefinition + R"FIDL(
type TheStruct = resource struct {
  some_handle handle:VMO;
  opt_one_more box<TheStruct>;
};
)FIDL");
  ASSERT_COMPILED(library);

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
                                 }));
  ASSERT_EQ(the_struct->members.size(), 2);
  ASSERT_NO_FAILURES(CheckFieldShape(the_struct->members[0], ExpectedField{
                                                                 .padding = 4,
                                                             }));
  ASSERT_NO_FAILURES(CheckFieldShape(the_struct->members[1], ExpectedField{
                                                                 .offset = 8,
                                                             }));
}

TEST(TypeshapeTests, GoodCoRecursiveStruct) {
  TestLibrary library(R"FIDL(library example;

type A = struct {
    foo box<B>;
};

type B = struct {
    bar box<A>;
};
)FIDL");
  ASSERT_COMPILED(library);

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

TEST(TypeshapeTests, GoodCoRecursiveStructWithHandles) {
  TestLibrary library(R"FIDL(
library example;
using zx;

type A = resource struct {
    a zx.handle;
    foo box<B>;
};

type B = resource struct {
    b zx.handle;
    bar box<A>;
};
)FIDL");
  library.UseLibraryZx();
  ASSERT_COMPILED(library);

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
                               }));
}

TEST(TypeshapeTests, GoodCoRecursiveStruct2) {
  TestLibrary library(R"FIDL(library example;

type Foo = struct {
    b Bar;
};

type Bar = struct {
    f box<Foo>;
};
)FIDL");
  ASSERT_COMPILED(library);

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

TEST(TypeshapeTests, GoodStructTwoDeep) {
  TestLibrary library(kPrologWithHandleDefinition + R"FIDL(
type DiffEntry = resource struct {
    key vector<uint8>:256;

    base box<Value>;
    left box<Value>;
    right box<Value>;
};

type Value = resource struct {
    value box<Buffer>;
    priority Priority;
};

type Buffer = resource struct {
    vmo handle:VMO;
    size uint64;
};

type Priority = enum {
    EAGER = 0;
    LAZY = 1;
};
)FIDL");
  ASSERT_COMPILED(library);

  auto buffer = library.LookupStruct("Buffer");
  ASSERT_NOT_NULL(buffer);
  ASSERT_NO_FAILURES(CheckTypeShape(buffer, Expected{
                                                .inline_size = 16,
                                                .alignment = 8,
                                                .max_handles = 1,
                                                .has_padding = true,
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
                                 }));
}

TEST(TypeshapeTests, GoodProtocolChildAndParent) {
  SharedAmongstLibraries shared;
  TestLibrary parent_library(&shared, "parent.fidl", R"FIDL(library parent;

protocol Parent {
    Sync() -> ();
};
)FIDL");
  ASSERT_COMPILED(parent_library);

  TestLibrary child_library(&shared, "child.fidl", R"FIDL(
library child;

using parent;

protocol Child {
  compose parent.Parent;
};
)FIDL");
  ASSERT_COMPILED(child_library);

  auto child = child_library.LookupProtocol("Child");
  ASSERT_NOT_NULL(child);
  ASSERT_EQ(child->all_methods.size(), 1);
  auto& sync_with_info = child->all_methods[0];
  auto sync_request = sync_with_info.method->maybe_request.get();
  EXPECT_EQ(sync_with_info.method->has_request, true);
  ASSERT_NULL(sync_request);
}

TEST(TypeshapeTests, GoodUnionSize8Alignment4Sandwich) {
  TestLibrary library(R"FIDL(library example;

type UnionSize8Alignment4 = strict union {
    1: variant uint32;
};

type Sandwich = struct {
    before uint32;
    union UnionSize8Alignment4;
    after uint32;
};
)FIDL");
  ASSERT_COMPILED(library);

  auto sandwich = library.LookupStruct("Sandwich");
  ASSERT_NOT_NULL(sandwich);
  ASSERT_NO_FAILURES(CheckTypeShape(sandwich,
                                    Expected{
                                        .inline_size = 40,
                                        .alignment = 8,
                                        .max_out_of_line = 8,
                                        .max_handles = 0,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 32,
                                        .alignment = 8,
                                        .max_out_of_line = 0,
                                        .max_handles = 0,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_envelope = true,
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
                                         .offset = 24,
                                         .padding = 4,
                                     }));
}

TEST(TypeshapeTests, GoodUnionSize12Alignment4Sandwich) {
  TestLibrary library(R"FIDL(library example;

type UnionSize12Alignment4 = strict union {
    1: variant array<uint8, 6>;
};

type Sandwich = struct {
    before uint32;
    union UnionSize12Alignment4;
    after int32;
};
)FIDL");
  ASSERT_COMPILED(library);

  auto sandwich = library.LookupStruct("Sandwich");
  ASSERT_NOT_NULL(sandwich);
  ASSERT_NO_FAILURES(CheckTypeShape(sandwich,
                                    Expected{
                                        .inline_size = 40,
                                        .alignment = 8,
                                        .max_out_of_line = 8,
                                        .max_handles = 0,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 32,
                                        .alignment = 8,
                                        .max_out_of_line = 8,
                                        .max_handles = 0,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_envelope = true,
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
                                         .offset = 24,
                                         .padding = 4,
                                     }));
}

TEST(TypeshapeTests, GoodUnionSize24Alignment8Sandwich) {
  TestLibrary library(R"FIDL(library example;

type StructSize16Alignment8 = struct {
    f1 uint64;
    f2 uint64;
};

type UnionSize24Alignment8 = strict union {
    1: variant StructSize16Alignment8;
};

type Sandwich = struct {
    before uint32;
    union UnionSize24Alignment8;
    after uint32;
};
)FIDL");
  ASSERT_COMPILED(library);

  auto sandwich = library.LookupStruct("Sandwich");
  ASSERT_NOT_NULL(sandwich);
  ASSERT_NO_FAILURES(CheckTypeShape(sandwich,
                                    Expected{
                                        .inline_size = 40,
                                        .alignment = 8,
                                        .max_out_of_line = 16,
                                        .max_handles = 0,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 32,
                                        .alignment = 8,
                                        .max_out_of_line = 16,
                                        .max_handles = 0,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_envelope = true,
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
                                         .offset = 24,
                                         .padding = 4,
                                     }));
}

TEST(TypeshapeTests, GoodUnionSize36Alignment4Sandwich) {
  TestLibrary library(R"FIDL(library example;

type UnionSize36Alignment4 = strict union {
    1: variant array<uint8, 32>;
};

type Sandwich = struct {
    before uint32;
    union UnionSize36Alignment4;
    after uint32;
};
)FIDL");
  ASSERT_COMPILED(library);

  auto sandwich = library.LookupStruct("Sandwich");
  ASSERT_NOT_NULL(sandwich);
  ASSERT_NO_FAILURES(CheckTypeShape(sandwich,
                                    Expected{
                                        .inline_size = 40,
                                        .alignment = 8,
                                        .max_out_of_line = 32,
                                        .max_handles = 0,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_envelope = true,
                                    },
                                    Expected{
                                        .inline_size = 32,
                                        .alignment = 8,
                                        .max_out_of_line = 32,
                                        .max_handles = 0,
                                        .depth = 1,
                                        .has_padding = true,
                                        .has_envelope = true,
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
                                         .offset = 24,
                                         .padding = 4,
                                     }));
}

TEST(TypeshapeTests, GoodZeroSizeVector) {
  TestLibrary library(R"FIDL(
library example;
using zx;

type A = resource struct {
    zero_size vector<zx.handle>:0;
};

)FIDL");
  library.UseLibraryZx();
  ASSERT_COMPILED(library);

  auto struct_a = library.LookupStruct("A");
  ASSERT_NOT_NULL(struct_a);
  ASSERT_NO_FAILURES(CheckTypeShape(struct_a, Expected{
                                                  .inline_size = 16,
                                                  .alignment = 8,
                                                  .max_out_of_line = 0,
                                                  .max_handles = 0,
                                                  .depth = 1,
                                                  .has_padding = true,
                                              }));
}

}  // namespace
