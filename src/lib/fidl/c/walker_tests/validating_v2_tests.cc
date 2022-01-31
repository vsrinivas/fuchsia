// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// NOTE: THIS IS A FORK OF validating_tests.cc AND WILL REPLACE THAT
// FILE ONCE THE MIGRATION TO THE V2 WIREFORMAT IS COMPLETE.

#include <lib/fidl/coding.h>
#include <stddef.h>

#include <cstdio>
#include <limits>
#include <memory>

#include <zxtest/zxtest.h>

#include "array_util.h"
#include "extra_messages.h"
#include "fidl_coded_types.h"
#include "fidl_structs.h"

namespace fidl {
namespace {

// Some notes:
//
// - All tests of out-of-line bounded allocation overruns need to have
//   another big out-of-line allocation following it. This
//   distinguishes "the buffer is too small" from "the bits on the
//   wire asked for more than the type allowed".

// TODO(kulakowski) Change the tests to check for more specific error
// values, once those are settled.

constexpr zx_handle_t dummy_handle_0 = static_cast<zx_handle_t>(23);
constexpr zx_handle_t dummy_handle_1 = static_cast<zx_handle_t>(24);
constexpr zx_handle_t dummy_handle_2 = static_cast<zx_handle_t>(25);
constexpr zx_handle_t dummy_handle_3 = static_cast<zx_handle_t>(26);
constexpr zx_handle_t dummy_handle_4 = static_cast<zx_handle_t>(27);
constexpr zx_handle_t dummy_handle_5 = static_cast<zx_handle_t>(28);
constexpr zx_handle_t dummy_handle_6 = static_cast<zx_handle_t>(29);
constexpr zx_handle_t dummy_handle_7 = static_cast<zx_handle_t>(30);
constexpr zx_handle_t dummy_handle_8 = static_cast<zx_handle_t>(31);
constexpr zx_handle_t dummy_handle_9 = static_cast<zx_handle_t>(32);
constexpr zx_handle_t dummy_handle_10 = static_cast<zx_handle_t>(33);
constexpr zx_handle_t dummy_handle_11 = static_cast<zx_handle_t>(34);
constexpr zx_handle_t dummy_handle_12 = static_cast<zx_handle_t>(35);
constexpr zx_handle_t dummy_handle_13 = static_cast<zx_handle_t>(36);
constexpr zx_handle_t dummy_handle_14 = static_cast<zx_handle_t>(37);
constexpr zx_handle_t dummy_handle_15 = static_cast<zx_handle_t>(38);
constexpr zx_handle_t dummy_handle_16 = static_cast<zx_handle_t>(39);
constexpr zx_handle_t dummy_handle_17 = static_cast<zx_handle_t>(40);
constexpr zx_handle_t dummy_handle_18 = static_cast<zx_handle_t>(41);
constexpr zx_handle_t dummy_handle_19 = static_cast<zx_handle_t>(42);
constexpr zx_handle_t dummy_handle_20 = static_cast<zx_handle_t>(43);
constexpr zx_handle_t dummy_handle_21 = static_cast<zx_handle_t>(44);
constexpr zx_handle_t dummy_handle_22 = static_cast<zx_handle_t>(45);
constexpr zx_handle_t dummy_handle_23 = static_cast<zx_handle_t>(46);
constexpr zx_handle_t dummy_handle_24 = static_cast<zx_handle_t>(47);
constexpr zx_handle_t dummy_handle_25 = static_cast<zx_handle_t>(48);
constexpr zx_handle_t dummy_handle_26 = static_cast<zx_handle_t>(49);
constexpr zx_handle_t dummy_handle_27 = static_cast<zx_handle_t>(50);
constexpr zx_handle_t dummy_handle_28 = static_cast<zx_handle_t>(51);
constexpr zx_handle_t dummy_handle_29 = static_cast<zx_handle_t>(52);

zx_status_t fidl_validate_v2_transactional(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                                           uint32_t num_handles, const char** error_msg_out) {
  uint8_t* trimmed_bytes;
  uint32_t trimmed_num_bytes;
  zx_status_t trim_status = ::fidl::internal::fidl_exclude_header_bytes(
      bytes, num_bytes, &trimmed_bytes, &trimmed_num_bytes, error_msg_out);
  if (unlikely(trim_status != ZX_OK)) {
    return ZX_ERR_INVALID_ARGS;
  }

  return internal__fidl_validate__v2__may_break(type, trimmed_bytes, trimmed_num_bytes, num_handles,
                                                error_msg_out);
}

TEST(NullParameters, validate_v2_null_validate_parameters) {
  zx_handle_t handles[] = {static_cast<zx_handle_t>(23)};

  // Null message type.
  {
    nonnullable_handle_message_layout message = {};
    message.inline_struct.handle = FIDL_HANDLE_PRESENT;
    const char* error = nullptr;
    auto status = fidl_validate_v2_transactional(
        nullptr, &message, sizeof(nonnullable_handle_message_layout), ArrayCount(handles), &error);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NOT_NULL(error);
  }

  // Null message.
  {
    const char* error = nullptr;
    auto status = internal__fidl_validate__v2__may_break(&nonnullable_handle_message_type, nullptr,
                                                         sizeof(nonnullable_handle_message_layout),
                                                         ArrayCount(handles), &error);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NOT_NULL(error);
  }

  // Zero handles, for a message that has a handle.
  {
    nonnullable_handle_message_layout message = {};
    message.inline_struct.handle = FIDL_HANDLE_PRESENT;
    const char* error = nullptr;
    auto status = internal__fidl_validate__v2__may_break(&nonnullable_handle_message_type, &message,
                                                         sizeof(nonnullable_handle_message_layout),
                                                         0, &error);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NOT_NULL(error);
  }

  // A null error string pointer is ok, though.
  {
    auto status = internal__fidl_validate__v2__may_break(nullptr, nullptr, 0u, 0u, nullptr);
    EXPECT_NE(status, ZX_OK);
  }

  // A null error is also ok in success cases.
  {
    nonnullable_handle_message_layout message = {};
    message.inline_struct.handle = FIDL_HANDLE_PRESENT;
    auto status = fidl_validate_v2_transactional(&nonnullable_handle_message_type, &message,
                                                 sizeof(nonnullable_handle_message_layout),
                                                 ArrayCount(handles), nullptr);
    EXPECT_EQ(status, ZX_OK);
  }
}

// The Walker tests are disabled for host because they depend on fidl
// generated LLCPP code that can't run on host.

// TODO(fxbug.dev/52382): Move this test to GIDL.
#ifdef __Fuchsia__
TEST(Walker, validate_v2_walker_recursive_struct_max_out_of_line_depth) {
  // Up to 32 out of line objects are allowed - here there are 33 pointers.
  uintptr_t message[34];
  for (int i = 0; i < 33; i++) {
    message[i] = 0xffffffffffffffff;
  }
  message[33] = 0;

  const char* error = nullptr;
  auto status = internal__fidl_validate__v2__may_break(&fidl_test_coding_RecursiveOptionalTable,
                                                       &message[0], sizeof(message), 0, &error);
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_STREQ(error, "recursion depth exceeded");

  // Reduce the max recursion depth by 1.
  status =
      internal__fidl_validate__v2__may_break(&fidl_test_coding_RecursiveOptionalTable, &message[1],
                                             sizeof(message) - sizeof(uintptr_t), 0, &error);
  EXPECT_EQ(status, ZX_OK);
}
#endif

// TODO(fxbug.dev/52382): Move this test to GIDL.
#ifdef __Fuchsia__
TEST(Walker, validate_v2_walker_table_max_out_of_line_depth_exceeded) {
  // 1 table + 31 non-null pointers + 1 null pointer = 33 out of line elements.
  uint8_t message[sizeof(fidl_vector_t) + sizeof(fidl_envelope_v2_t) + sizeof(uintptr_t) * 32];
  fidl_vector_t* vec = reinterpret_cast<fidl_vector_t*>(message);
  fidl_envelope_v2_t* envelope =
      reinterpret_cast<fidl_envelope_v2_t*>(message + sizeof(fidl_vector_t));
  uintptr_t* opt_structs =
      reinterpret_cast<uintptr_t*>(message + sizeof(fidl_vector_t) + sizeof(fidl_envelope_v2_t));
  vec->count = 1;
  vec->data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
  envelope->num_bytes = 256;
  envelope->num_handles = 0;
  for (int i = 0; i < 31; i++) {
    opt_structs[i] = FIDL_ALLOC_PRESENT;
  }
  opt_structs[31] = 0;

  const char* error = nullptr;
  auto status = internal__fidl_validate__v2__may_break(&fidl_test_coding_RecursiveTableTable,
                                                       &message[0], sizeof(message), 0, &error);
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_STREQ(error, "recursion depth exceeded");
}

// TODO(fxbug.dev/52382): Move this test to GIDL.
TEST(Walker, validate_v2_walker_table_max_out_of_line_depth_matched) {
  // 1 table + 30 non-null pointers + 1 null pointer = 32 out of line elements.
  uint8_t message[sizeof(fidl_vector_t) + sizeof(fidl_envelope_v2_t) + sizeof(uintptr_t) * 31];
  fidl_vector_t* vec = reinterpret_cast<fidl_vector_t*>(message);
  fidl_envelope_v2_t* envelope =
      reinterpret_cast<fidl_envelope_v2_t*>(message + sizeof(fidl_vector_t));
  uintptr_t* opt_structs =
      reinterpret_cast<uintptr_t*>(message + sizeof(fidl_vector_t) + sizeof(fidl_envelope_v2_t));
  vec->count = 1;
  vec->data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
  envelope->num_bytes = 248;
  envelope->num_handles = 0;
  for (int i = 0; i < 30; i++) {
    opt_structs[i] = FIDL_ALLOC_PRESENT;
  }
  opt_structs[30] = 0;

  const char* error = nullptr;
  auto status = internal__fidl_validate__v2__may_break(&fidl_test_coding_RecursiveTableTable,
                                                       &message[0], sizeof(message), 0, &error);
  EXPECT_EQ(status, ZX_OK);
}
#endif

TEST(BufferTooSmall, validate_v2_overflow_buffer_on_FidlAlign) {
  // Message: Struct with 1 1-byte (uint8) field.
  // Field type.
  const FidlCodedPrimitive element_field_type = {
      .tag = kFidlTypePrimitive,
      .type = kFidlCodedPrimitiveSubtype_Uint8,
  };
  // Field.
  const FidlStructElement element = {
      .field =
          {
              .header =
                  {
                      .element_type = kFidlStructElementType_Field,
                      .is_resource = kFidlIsResource_NotResource,
                  },
              .offset_v1 = 0,
              .offset_v2 = 0,
              .field_type = &element_field_type,
          },
  };
  // Struct.
  const FidlCodedStruct type = {
      .tag = kFidlTypeStruct,
      .element_count = 1,
      .size_v1 = 1,
      .size_v2 = 1,
      .elements = &element,
      .name = nullptr,
  };
  // Message: Aligned and 0-padded to exercise checks after 0-pad check.
  alignas(FIDL_ALIGNMENT) uint8_t message[2 * FIDL_ALIGNMENT] = {};
  const char* error = nullptr;

  // Message intended to contain 1 byte (though more bytes prepared/0-padded).
  auto status = internal__fidl_validate__v2__may_break(&type, &message, 1, 0, &error);

  // Expect error to be something about buffer too small (for for properly padded message).
  EXPECT_EQ(status, ZX_ERR_BUFFER_TOO_SMALL);
  EXPECT_NOT_NULL(error);
  ASSERT_SUBSTR(error, "too small");
}

TEST(Handles, validate_v2_single_present_handle_unaligned_error) {
  // Test a short, unaligned version of nonnullable message
  // handle. All fidl message objects should be 8 byte aligned.
  //
  // We use a 16 bytes array rather than fidl_message_header_t, and
  // manually place the |message| structure at a 4 bytes offset,
  // to avoid aligning to 8 bytes.
  struct unaligned_nonnullable_handle_inline_data {
    uint8_t header[sizeof(fidl_message_header_t)];
    zx_handle_t handle;
  };
  struct unaligned_nonnullable_handle_message_layout {
    unaligned_nonnullable_handle_inline_data inline_struct;
  };

  uint8_t message_buffer[FIDL_ALIGN(sizeof(unaligned_nonnullable_handle_message_layout) +
                                    sizeof(zx_handle_t))] = {};
  unaligned_nonnullable_handle_message_layout& message =
      *reinterpret_cast<unaligned_nonnullable_handle_message_layout*>(
          &message_buffer[sizeof(zx_handle_t)]);
  message.inline_struct.handle = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[] = {
      dummy_handle_0,
  };

  // Validating the unaligned version of the struct should fail.
  const char* error = nullptr;
  auto status = fidl_validate_v2_transactional(&nonnullable_handle_message_type, &message,
                                               sizeof(message), ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
}

TEST(Structs, validate_v2_nested_nonnullable_structs) {
  nested_structs_message_layout message = {};
  message.inline_struct.l0.handle_0 = FIDL_HANDLE_PRESENT;
  message.inline_struct.l0.l1.handle_1 = FIDL_HANDLE_PRESENT;
  message.inline_struct.l0.l1.l2.handle_2 = FIDL_HANDLE_PRESENT;
  message.inline_struct.l0.l1.l2.l3.handle_3 = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[] = {
      dummy_handle_0,
      dummy_handle_1,
      dummy_handle_2,
      dummy_handle_3,
  };

  const char* error = nullptr;
  auto status = fidl_validate_v2_transactional(&nested_structs_message_type, &message,
                                               sizeof(message), ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  // Note the traversal order! l1 -> l3 -> l2 -> l0
  EXPECT_EQ(message.inline_struct.l0.l1.handle_1, FIDL_HANDLE_PRESENT);
  EXPECT_EQ(message.inline_struct.l0.l1.l2.l3.handle_3, FIDL_HANDLE_PRESENT);
  EXPECT_EQ(message.inline_struct.l0.l1.l2.handle_2, FIDL_HANDLE_PRESENT);
  EXPECT_EQ(message.inline_struct.l0.handle_0, FIDL_HANDLE_PRESENT);
}

TEST(Structs, validate_v2_nested_nonnullable_structs_check_padding) {
  // Wire-format:
  // message
  // - 16 bytes header
  // + struct_level_0  -------------  offset 16 = 4 * 4
  //   - uint64_t
  //   + struct_level_1  -----------  offset 24 = 4 * 6
  //     - zx_handle_t
  //     - (4 bytes padding)  ------  offset 28 = 4 * 7
  //     + struct_level_2  ---------  offset 32 = 4 * 8
  //       - uint64_t
  //       + struct_level_3  -------  offset 40 = 4 * 10
  //         - uint32_t
  //         - zx_handle_t
  //       - zx_handle_t
  //       - (4 bytes padding)  ----  offset 52 = 4 * 13
  //     - uint64_t
  //   - zx_handle_t
  //   - (4 bytes padding)  --------  offset 68 = 4 * 17
  static_assert(sizeof(nested_structs_message_layout) == 68 + 4);
  // Hence the padding bytes are located at:
  size_t padding_offsets[] = {
      28, 29, 30, 31, 52, 53, 54, 55, 68, 69, 70, 71,
  };

  for (const auto padding_offset : padding_offsets) {
    constexpr size_t kBufferSize = sizeof(nested_structs_message_layout);
    nested_structs_message_layout message;
    uint8_t* buffer = reinterpret_cast<uint8_t*>(&message);
    memset(buffer, 0, kBufferSize);

    message.inline_struct.l0.handle_0 = FIDL_HANDLE_PRESENT;
    message.inline_struct.l0.l1.handle_1 = FIDL_HANDLE_PRESENT;
    message.inline_struct.l0.l1.l2.handle_2 = FIDL_HANDLE_PRESENT;
    message.inline_struct.l0.l1.l2.l3.handle_3 = FIDL_HANDLE_PRESENT;
    constexpr uint32_t kNumHandles = 4;

    buffer[padding_offset] = 0xAA;

    const char* error = nullptr;
    auto status = fidl_validate_v2_transactional(&nested_structs_message_type, &message,
                                                 kBufferSize, kNumHandles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    ASSERT_NOT_NULL(error);
    EXPECT_STREQ(error, "non-zero padding bytes detected");
  }
}

TEST(Structs, validate_v2_nested_nullable_structs) {
  // See below for the handle traversal order.
  nested_struct_ptrs_message_layout message = {};

  message.inline_struct.l0_present = reinterpret_cast<struct_ptr_level_0*>(FIDL_ALLOC_PRESENT);
  message.inline_struct.l0_inline.l1_present =
      reinterpret_cast<struct_ptr_level_1*>(FIDL_ALLOC_PRESENT);
  message.inline_struct.l0_inline.l1_inline.l2_present =
      reinterpret_cast<struct_ptr_level_2*>(FIDL_ALLOC_PRESENT);
  message.inline_struct.l0_inline.l1_inline.l2_inline.l3_present =
      reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_PRESENT);
  message.in_in_out_2.l3_present = reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_PRESENT);
  message.in_out_1.l2_present = reinterpret_cast<struct_ptr_level_2*>(FIDL_ALLOC_PRESENT);
  message.in_out_1.l2_inline.l3_present = reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_PRESENT);
  message.in_out_out_2.l3_present = reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_PRESENT);
  message.out_0.l1_present = reinterpret_cast<struct_ptr_level_1*>(FIDL_ALLOC_PRESENT);
  message.out_0.l1_inline.l2_present = reinterpret_cast<struct_ptr_level_2*>(FIDL_ALLOC_PRESENT);
  message.out_0.l1_inline.l2_inline.l3_present =
      reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_PRESENT);
  message.out_in_out_2.l3_present = reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_PRESENT);
  message.out_out_1.l2_present = reinterpret_cast<struct_ptr_level_2*>(FIDL_ALLOC_PRESENT);
  message.out_out_1.l2_inline.l3_present =
      reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_PRESENT);
  message.out_out_out_2.l3_present = reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_PRESENT);

  message.inline_struct.l0_absent = reinterpret_cast<struct_ptr_level_0*>(FIDL_ALLOC_ABSENT);
  message.inline_struct.l0_inline.l1_absent =
      reinterpret_cast<struct_ptr_level_1*>(FIDL_ALLOC_ABSENT);
  message.inline_struct.l0_inline.l1_inline.l2_absent =
      reinterpret_cast<struct_ptr_level_2*>(FIDL_ALLOC_ABSENT);
  message.inline_struct.l0_inline.l1_inline.l2_inline.l3_absent =
      reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_ABSENT);
  message.in_in_out_2.l3_absent = reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_ABSENT);
  message.in_out_1.l2_absent = reinterpret_cast<struct_ptr_level_2*>(FIDL_ALLOC_ABSENT);
  message.in_out_1.l2_inline.l3_absent = reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_ABSENT);
  message.in_out_out_2.l3_absent = reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_ABSENT);
  message.out_0.l1_absent = reinterpret_cast<struct_ptr_level_1*>(FIDL_ALLOC_ABSENT);
  message.out_0.l1_inline.l2_absent = reinterpret_cast<struct_ptr_level_2*>(FIDL_ALLOC_ABSENT);
  message.out_0.l1_inline.l2_inline.l3_absent =
      reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_ABSENT);
  message.out_in_out_2.l3_absent = reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_ABSENT);
  message.out_out_1.l2_absent = reinterpret_cast<struct_ptr_level_2*>(FIDL_ALLOC_ABSENT);
  message.out_out_1.l2_inline.l3_absent = reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_ABSENT);
  message.out_out_out_2.l3_absent = reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_ABSENT);

  message.inline_struct.l0_inline.l1_inline.handle_1 = FIDL_HANDLE_PRESENT;
  message.in_in_out_out_3.handle_3 = FIDL_HANDLE_PRESENT;
  message.in_in_out_2.l3_inline.handle_3 = FIDL_HANDLE_PRESENT;
  message.in_in_out_2.handle_2 = FIDL_HANDLE_PRESENT;
  message.in_in_in_out_3.handle_3 = FIDL_HANDLE_PRESENT;
  message.inline_struct.l0_inline.l1_inline.l2_inline.l3_inline.handle_3 = FIDL_HANDLE_PRESENT;
  message.inline_struct.l0_inline.l1_inline.l2_inline.handle_2 = FIDL_HANDLE_PRESENT;
  message.inline_struct.l0_inline.handle_0 = FIDL_HANDLE_PRESENT;
  message.in_out_1.handle_1 = FIDL_HANDLE_PRESENT;
  message.in_out_out_out_3.handle_3 = FIDL_HANDLE_PRESENT;
  message.in_out_out_2.l3_inline.handle_3 = FIDL_HANDLE_PRESENT;
  message.in_out_out_2.handle_2 = FIDL_HANDLE_PRESENT;
  message.in_out_in_out_3.handle_3 = FIDL_HANDLE_PRESENT;
  message.in_out_1.l2_inline.l3_inline.handle_3 = FIDL_HANDLE_PRESENT;
  message.in_out_1.l2_inline.handle_2 = FIDL_HANDLE_PRESENT;
  message.out_0.l1_inline.handle_1 = FIDL_HANDLE_PRESENT;
  message.out_in_out_out_3.handle_3 = FIDL_HANDLE_PRESENT;
  message.out_in_out_2.l3_inline.handle_3 = FIDL_HANDLE_PRESENT;
  message.out_in_out_2.handle_2 = FIDL_HANDLE_PRESENT;
  message.out_in_in_out_3.handle_3 = FIDL_HANDLE_PRESENT;
  message.out_0.l1_inline.l2_inline.l3_inline.handle_3 = FIDL_HANDLE_PRESENT;
  message.out_0.l1_inline.l2_inline.handle_2 = FIDL_HANDLE_PRESENT;
  message.out_0.handle_0 = FIDL_HANDLE_PRESENT;
  message.out_out_1.handle_1 = FIDL_HANDLE_PRESENT;
  message.out_out_out_out_3.handle_3 = FIDL_HANDLE_PRESENT;
  message.out_out_out_2.l3_inline.handle_3 = FIDL_HANDLE_PRESENT;
  message.out_out_out_2.handle_2 = FIDL_HANDLE_PRESENT;
  message.out_out_in_out_3.handle_3 = FIDL_HANDLE_PRESENT;
  message.out_out_1.l2_inline.l3_inline.handle_3 = FIDL_HANDLE_PRESENT;
  message.out_out_1.l2_inline.handle_2 = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[] = {
      dummy_handle_0,  dummy_handle_1,  dummy_handle_2,  dummy_handle_3,  dummy_handle_4,
      dummy_handle_5,  dummy_handle_6,  dummy_handle_7,  dummy_handle_8,  dummy_handle_9,
      dummy_handle_10, dummy_handle_11, dummy_handle_12, dummy_handle_13, dummy_handle_14,
      dummy_handle_15, dummy_handle_16, dummy_handle_17, dummy_handle_18, dummy_handle_19,
      dummy_handle_20, dummy_handle_21, dummy_handle_22, dummy_handle_23, dummy_handle_24,
      dummy_handle_25, dummy_handle_26, dummy_handle_27, dummy_handle_28, dummy_handle_29,
  };

  const char* error = nullptr;
  auto status = fidl_validate_v2_transactional(&nested_struct_ptrs_message_type, &message,
                                               sizeof(message), ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
}

TEST(Xunions, validate_v2_valid_empty_nullable_xunion) {
  SampleNullableXUnionV2Struct message = {};

  const char* error = nullptr;
  auto status =
      internal__fidl_validate__v2__may_break(&fidl_test_coding_SampleNullableXUnionStructTable,
                                             &message, sizeof(fidl_xunion_v2_t), 0, &error);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
}

TEST(Xunions, validate_v2_empty_nonnullable_xunion) {
  SampleXUnionV2Struct message = {};

  const char* error = nullptr;
  auto status = internal__fidl_validate__v2__may_break(
      &fidl_test_coding_SampleXUnionStructTable, &message, sizeof(fidl_xunion_v2_t), 0, &error);
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
  EXPECT_STREQ(error, "non-nullable xunion is absent");
}

TEST(Xunions, validate_v2_empty_nullable_xunion_nonzero_ordinal) {
  SampleNullableXUnionV2Struct message = {};
  message.opt_xu.header.tag = kSampleXUnionIntStructOrdinal;

  const char* error = nullptr;
  auto status =
      internal__fidl_validate__v2__may_break(&fidl_test_coding_SampleNullableXUnionStructTable,
                                             &message, sizeof(fidl_xunion_v2_t), 0, &error);
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
  EXPECT_STREQ(error, "empty xunion must have zero as ordinal");
}

TEST(Xunions, validate_v2_nonempty_xunion_zero_ordinal) {
  SampleXUnionV2Struct message = {};
  message.xu.header.envelope = (fidl_envelope_v2_t){.num_bytes = 8, .num_handles = 0};

  const char* error = nullptr;
  auto status = internal__fidl_validate__v2__may_break(
      &fidl_test_coding_SampleXUnionStructTable, &message, sizeof(SampleXUnionV2Struct), 0, &error);
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
  EXPECT_STREQ(error, "xunion with zero as ordinal must be empty");
}

TEST(Xunions, validate_v2_nonempty_nullable_xunion_zero_ordinal) {
  SampleNullableXUnionV2Struct message = {};
  message.opt_xu.header.envelope = (fidl_envelope_v2_t){.num_bytes = 8, .num_handles = 0};

  const char* error = nullptr;
  auto status = internal__fidl_validate__v2__may_break(
      &fidl_test_coding_SampleNullableXUnionStructTable, &message,
      sizeof(SampleNullableXUnionV2Struct), 0, &error);
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
  EXPECT_STREQ(error, "xunion with zero as ordinal must be empty");
}

TEST(Xunions, validate_v2_strict_xunion_unknown_ordinal) {
  uint8_t bytes[] = {
      0xf0, 0x05, 0xc1, 0x0a,                          // invalid ordinal
      0x00, 0x00, 0x00, 0x00,                          // padding
      0x08, 0x00, 0x00, 0x00,                          // envelope: # of bytes
      0x00, 0x00, 0x00, 0x00,                          // envelope: # of handles
      0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00,  // fake out-of-line data
  };

  const char* error = nullptr;
  auto status = internal__fidl_validate__v2__may_break(
      &fidl_test_coding_SampleStrictXUnionStructTable, bytes, sizeof(bytes), 0, &error);
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
  EXPECT_STREQ(error, "strict xunion has unknown ordinal");
}

TEST(Xunions, validate_v2_flexible_xunion_unknown_ordinal) {
  uint8_t bytes[] = {
      0xf0, 0x05, 0xc1, 0x0a,                          // invalid ordinal
      0x00, 0x00, 0x00, 0x00,                          // padding
      0x08, 0x00, 0x00, 0x00,                          // envelope: # of bytes
      0x00, 0x00, 0x00, 0x00,                          // envelope: # of handles
      0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00,  // fake out-of-line data
  };

  const char* error = nullptr;
  auto status = internal__fidl_validate__v2__may_break(&fidl_test_coding_SampleXUnionStructTable,
                                                       bytes, sizeof(bytes), 0, &error);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error);
}

TEST(Primitives, validate_v2_invalid_bool) {
  uint8_t data[] = {
      0x88,  // bool, not 0 or 1*/
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };

  const char* error = nullptr;
  fflush(stdout);
  auto status = internal__fidl_validate__v2__may_break(&fidl_test_coding_BoolStructTable, data,
                                                       sizeof(data), 0, &error);
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_STREQ(error, "not a valid bool value");
}

TEST(Bits, validate_v2_zero_16bit_bits) {
  Int16Bits message;
  memset(std::launder(&message), 0, sizeof(message));
  message.bits = 0;

  const char* error = nullptr;
  auto status = internal__fidl_validate__v2__may_break(&fidl_test_coding_Int16BitsStructTable,
                                                       &message, sizeof(message), 0, &error);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
}

TEST(Bits, validate_v2_valid_16bit_bits) {
  Int16Bits message;
  memset(std::launder(&message), 0, sizeof(message));
  message.bits = 1u | 16u;

  const char* error = nullptr;
  auto status = internal__fidl_validate__v2__may_break(&fidl_test_coding_Int16BitsStructTable,
                                                       &message, sizeof(message), 0, &error);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
}

TEST(Bits, validate_v2_invalid_16bit_bits) {
  Int16Bits message;
  memset(std::launder(&message), 0, sizeof(message));
  message.bits = 1u << 7u;

  const char* error = nullptr;
  auto status = internal__fidl_validate__v2__may_break(&fidl_test_coding_Int16BitsStructTable,
                                                       &message, sizeof(message), 0, &error);
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_STREQ(error, "not a valid bits member");
}

TEST(Bits, validate_v2_zero_32bit_bits) {
  Int32Bits message;
  memset(std::launder(&message), 0, sizeof(message));
  message.bits = 0;

  const char* error = nullptr;
  auto status = internal__fidl_validate__v2__may_break(&fidl_test_coding_Int32BitsStructTable,
                                                       &message, sizeof(message), 0, &error);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
}

TEST(Bits, validate_v2_valid_32bit_bits) {
  // The valid bits are position 7, 12, and 27.
  Int32Bits message;
  memset(std::launder(&message), 0, sizeof(message));
  message.bits = (1u << 6u) | (1u << 11u) | (1u << 26u);

  const char* error = nullptr;
  auto status = internal__fidl_validate__v2__may_break(&fidl_test_coding_Int32BitsStructTable,
                                                       &message, sizeof(message), 0, &error);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
}

TEST(Bits, validate_v2_invalid_32bit_bits) {
  // The valid bits are position 7, 12, and 27.
  Int32Bits message;
  memset(std::launder(&message), 0, sizeof(message));
  message.bits = 1u;

  const char* error = nullptr;
  auto status = internal__fidl_validate__v2__may_break(&fidl_test_coding_Int32BitsStructTable,
                                                       &message, sizeof(message), 0, &error);
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_STREQ(error, "not a valid bits member");
}

template <typename T>
void TestValidEnum(const fidl_type_t* coding_table) {
  // See extra_messages.test.fidl for the list of valid members.
  using Underlying = decltype(T::e);
  for (const Underlying valid_value : {
           static_cast<Underlying>(42),
           std::numeric_limits<Underlying>::min(),
           std::numeric_limits<Underlying>::max(),
       }) {
    T message;
    memset(std::launder(&message), 0, sizeof(message));
    message.e = valid_value;
    const char* error = nullptr;
    auto status =
        internal__fidl_validate__v2__may_break(coding_table, &message, sizeof(message), 0, &error);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error);
  }
}

template <typename T>
void TestInvalidEnum(const fidl_type_t* coding_table) {
  // See extra_messages.test.fidl for the list of valid members.
  using Underlying = decltype(T::e);
  for (const Underlying invalid_value : {
           static_cast<Underlying>(7),
           static_cast<Underlying>(30),
           static_cast<Underlying>(std::numeric_limits<Underlying>::min() + 1),
           static_cast<Underlying>(std::numeric_limits<Underlying>::max() - 1),
       }) {
    T message;
    memset(std::launder(&message), 0, sizeof(message));
    message.e = invalid_value;
    const char* error = nullptr;
    auto status =
        internal__fidl_validate__v2__may_break(coding_table, &message, sizeof(message), 0, &error);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_STREQ(error, "not a valid enum member");
  }
}

TEST(Enums, validate_v2_int8_enum) {
  TestValidEnum<Int8Enum>(&fidl_test_coding_Int8EnumStructTable);
}

TEST(Enums, validate_v2_int16_enum) {
  TestValidEnum<Int16Enum>(&fidl_test_coding_Int16EnumStructTable);
}

TEST(Enums, validate_v2_int32_enum) {
  TestValidEnum<Int32Enum>(&fidl_test_coding_Int32EnumStructTable);
}

TEST(Enums, validate_v2_int64_enum) {
  TestValidEnum<Int64Enum>(&fidl_test_coding_Int64EnumStructTable);
}

TEST(Enums, validate_v2_uint8_enum) {
  TestValidEnum<Uint8Enum>(&fidl_test_coding_Uint8EnumStructTable);
}

TEST(Enums, validate_v2_uint16_enum) {
  TestValidEnum<Uint16Enum>(&fidl_test_coding_Uint16EnumStructTable);
}

TEST(Enums, validate_v2_uint32_enum) {
  TestValidEnum<Uint32Enum>(&fidl_test_coding_Uint32EnumStructTable);
}

TEST(Enums, validate_v2_uint64_enum) {
  TestValidEnum<Uint64Enum>(&fidl_test_coding_Uint64EnumStructTable);
}

TEST(Enums, validate_v2_invalid_int8_enum) {
  TestInvalidEnum<Int8Enum>(&fidl_test_coding_Int8EnumStructTable);
}

TEST(Enums, validate_v2_invalid_int16_enum) {
  TestInvalidEnum<Int16Enum>(&fidl_test_coding_Int16EnumStructTable);
}

TEST(Enums, validate_v2_invalid_int32_enum) {
  TestInvalidEnum<Int32Enum>(&fidl_test_coding_Int32EnumStructTable);
}

TEST(Enums, validate_v2_invalid_int64_enum) {
  TestInvalidEnum<Int64Enum>(&fidl_test_coding_Int64EnumStructTable);
}

TEST(Enums, validate_v2_invalid_uint8_enum) {
  TestInvalidEnum<Uint8Enum>(&fidl_test_coding_Uint8EnumStructTable);
}

TEST(Enums, validate_v2_invalid_uint16_enum) {
  TestInvalidEnum<Uint16Enum>(&fidl_test_coding_Uint16EnumStructTable);
}

TEST(Enums, validate_v2_invalid_uint32_enum) {
  TestInvalidEnum<Uint32Enum>(&fidl_test_coding_Uint32EnumStructTable);
}

TEST(Enums, validate_v2_invalid_uint64_enum) {
  TestInvalidEnum<Uint64Enum>(&fidl_test_coding_Uint64EnumStructTable);
}

TEST(Primitives, validate_v2_primitives_struct) {
  // TODO(fxbug.dev/52585): Use generated types - primitive struct fields actually have null type.
  // The following coding table is equivalent to this FIDL struct definition:
  //
  // struct PrimitiveStruct {
  //   bool b;
  //   int8 i8;
  //   int16 i16;
  //   int32 i32;
  //   int64 i64;
  //   uint8 u8;
  //   uint16 u16;
  //   uint32 u32;
  //   uint64 u64;
  //   float32 f32;
  //   float64 f64;
  // };
  static const FidlCodedPrimitive kBoolType = {.tag = kFidlTypePrimitive,
                                               .type = kFidlCodedPrimitiveSubtype_Bool};
  static const FidlCodedPrimitive kInt8Type = {.tag = kFidlTypePrimitive,
                                               .type = kFidlCodedPrimitiveSubtype_Int8};
  static const FidlCodedPrimitive kInt16Type = {.tag = kFidlTypePrimitive,
                                                .type = kFidlCodedPrimitiveSubtype_Int16};
  static const FidlCodedPrimitive kInt32Type = {.tag = kFidlTypePrimitive,
                                                .type = kFidlCodedPrimitiveSubtype_Int32};
  static const FidlCodedPrimitive kInt64Type = {.tag = kFidlTypePrimitive,
                                                .type = kFidlCodedPrimitiveSubtype_Int64};
  static const FidlCodedPrimitive kUint8Type = {.tag = kFidlTypePrimitive,
                                                .type = kFidlCodedPrimitiveSubtype_Uint8};
  static const FidlCodedPrimitive kUint16Type = {.tag = kFidlTypePrimitive,
                                                 .type = kFidlCodedPrimitiveSubtype_Uint16};
  static const FidlCodedPrimitive kUint32Type = {.tag = kFidlTypePrimitive,
                                                 .type = kFidlCodedPrimitiveSubtype_Uint32};
  static const FidlCodedPrimitive kUint64Type = {.tag = kFidlTypePrimitive,
                                                 .type = kFidlCodedPrimitiveSubtype_Uint64};
  static const FidlCodedPrimitive kFloat32Type = {.tag = kFidlTypePrimitive,
                                                  .type = kFidlCodedPrimitiveSubtype_Float32};
  static const FidlCodedPrimitive kFloat64Type = {.tag = kFidlTypePrimitive,
                                                  .type = kFidlCodedPrimitiveSubtype_Float64};
  static const struct FidlStructElement kFields[] = {
      FidlStructElement::Field(&kBoolType, 0u, 0u, kFidlIsResource_NotResource),
      FidlStructElement::Field(&kInt8Type, 1u, 1u, kFidlIsResource_NotResource),
      FidlStructElement::Field(&kInt16Type, 2u, 2u, kFidlIsResource_NotResource),
      FidlStructElement::Field(&kInt32Type, 4u, 4u, kFidlIsResource_NotResource),
      FidlStructElement::Field(&kInt64Type, 8u, 8u, kFidlIsResource_NotResource),
      FidlStructElement::Field(&kUint8Type, 16u, 16u, kFidlIsResource_NotResource),
      FidlStructElement::Padding16(16u, 16u, 0x00ff),
      FidlStructElement::Field(&kUint16Type, 18u, 18u, kFidlIsResource_NotResource),
      FidlStructElement::Field(&kUint32Type, 20u, 20u, kFidlIsResource_NotResource),
      FidlStructElement::Field(&kUint64Type, 24u, 24u, kFidlIsResource_NotResource),
      FidlStructElement::Field(&kFloat32Type, 32u, 32u, kFidlIsResource_NotResource),
      FidlStructElement::Padding32(36u, 36u, 0xffffffff),
      FidlStructElement::Field(&kFloat64Type, 40u, 40u, kFidlIsResource_NotResource),
  };
  static const FidlCodedStruct kPrimitiveStructCodingTable = {
      .tag = kFidlTypeStruct,
      .element_count = ArrayCount(kFields),
      .size_v1 = 48u,
      .size_v2 = 48u,
      .elements = kFields,
      .name = "fidl.test.coding/PrimitiveStruct",
  };

  uint8_t data[kPrimitiveStructCodingTable.coded_struct().size_v1];
  memset(data, 0, sizeof(data));

  const char* error = nullptr;
  auto status = internal__fidl_validate__v2__may_break(
      &kPrimitiveStructCodingTable, data, static_cast<uint32_t>(sizeof(data)), 0, &error);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error);
}

}  // namespace
}  // namespace fidl
