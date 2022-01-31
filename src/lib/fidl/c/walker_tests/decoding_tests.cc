// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>
#include <lib/fidl/internal.h>
#include <stddef.h>
#include <zircon/errors.h>

#ifdef __Fuchsia__
#include <lib/zx/eventpair.h>
#include <zircon/syscalls.h>
#endif

#include <fidl/fidl.test.coding/cpp/wire.h>

#include <memory>

#include <zxtest/zxtest.h>

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

#ifdef __Fuchsia__
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
#endif

zx_status_t fidl_decode(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                        const zx_handle_t* handles, uint32_t num_handles,
                        const char** error_msg_out) {
  if (handles == nullptr) {
    return fidl_decode_etc(type, bytes, num_bytes, nullptr, num_handles, error_msg_out);
  }

  std::vector<zx_handle_info_t> handle_infos;
  for (uint32_t i = 0; i < num_handles; i++) {
    handle_infos.push_back({
        .handle = handles[i],
        .type = ZX_OBJ_TYPE_NONE,
        .rights = ZX_RIGHT_SAME_RIGHTS,
    });
  }

  return fidl_decode_etc(type, bytes, num_bytes, handle_infos.data(), handle_infos.size(),
                         error_msg_out);
}

zx_status_t fidl_decode_transactional(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                                      const zx_handle_t* handles, uint32_t num_handles,
                                      const char** error_msg_out) {
  uint8_t* trimmed_bytes;
  uint32_t trimmed_num_bytes;
  zx_status_t trim_status = ::fidl::internal::fidl_exclude_header_bytes(
      bytes, num_bytes, &trimmed_bytes, &trimmed_num_bytes, error_msg_out);
  if (unlikely(trim_status != ZX_OK)) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (trimmed_num_bytes == 0) {
    return ZX_OK;
  }

  return fidl_decode(type, trimmed_bytes, trimmed_num_bytes, handles, num_handles, error_msg_out);
}

#ifdef __Fuchsia__
zx_status_t fidl_decode_etc_transactional(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                                          const zx_handle_info_t* handle_infos,
                                          uint32_t num_handles, const char** error_msg_out) {
  uint8_t* trimmed_bytes;
  uint32_t trimmed_num_bytes;
  zx_status_t trim_status = ::fidl::internal::fidl_exclude_header_bytes(
      bytes, num_bytes, &trimmed_bytes, &trimmed_num_bytes, error_msg_out);
  if (unlikely(trim_status != ZX_OK)) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (trimmed_num_bytes == 0) {
    return ZX_OK;
  }

  return fidl_decode_etc(type, trimmed_bytes, trimmed_num_bytes, handle_infos, num_handles,
                         error_msg_out);
}
#endif  // __Fuchsia__

// All sizes in fidl encoding tables are 32 bits. The fidl compiler
// normally enforces this. Check manually in manual tests.
template <typename T, size_t N>
uint32_t ArrayCount(T const (&array)[N]) {
  static_assert(N < UINT32_MAX, "Array is too large!");
  return N;
}

template <typename T, size_t N>
uint32_t ArraySize(T const (&array)[N]) {
  static_assert(sizeof(array) < UINT32_MAX, "Array is too large!");
  return sizeof(array);
}

TEST(NullParameters, decode_null_decode_parameters) {
  zx_handle_t handles[] = {static_cast<zx_handle_t>(23)};

// Null message type.
#ifdef __Fuchsia__
  {
    nonnullable_handle_message_layout message = {};
    message.inline_struct.handle = FIDL_HANDLE_PRESENT;
    const char* error = nullptr;
    auto status =
        fidl_decode_transactional(nullptr, &message, sizeof(nonnullable_handle_message_layout),
                                  handles, ArrayCount(handles), &error);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NOT_NULL(error);
  }

  // Null message.
  {
    const char* error = nullptr;
    auto status = fidl_decode(&nonnullable_handle_message_type, nullptr,
                              sizeof(nonnullable_handle_message_layout), handles,
                              ArrayCount(handles), &error);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NOT_NULL(error);
  }
#endif  // __Fuchsia__

  // Null handles, for a message that has a handle.
  {
    nonnullable_handle_message_layout message = {};
    message.inline_struct.handle = FIDL_HANDLE_PRESENT;
    const char* error = nullptr;
    auto status =
        fidl_decode_transactional(&nonnullable_handle_message_type, &message,
                                  sizeof(nonnullable_handle_message_layout), nullptr, 0, &error);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NOT_NULL(error);
  }

  // Null handles but positive handle count.
  {
    nonnullable_handle_message_layout message = {};
    message.inline_struct.handle = FIDL_HANDLE_PRESENT;
    const char* error = nullptr;
    auto status =
        fidl_decode_transactional(&nonnullable_handle_message_type, &message,
                                  sizeof(nonnullable_handle_message_layout), nullptr, 1, &error);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NOT_NULL(error);
  }

  // A null error string pointer is ok, though.
  {
    auto status = fidl_decode(nullptr, nullptr, 0u, nullptr, 0u, nullptr);
    EXPECT_NE(status, ZX_OK);
  }

  // A null error is also ok in success cases.
  {
    nonnullable_handle_message_layout message = {};
    message.inline_struct.handle = FIDL_HANDLE_PRESENT;
    auto status = fidl_decode_transactional(&nonnullable_handle_message_type, &message,
                                            sizeof(nonnullable_handle_message_layout), handles,
                                            ArrayCount(handles), nullptr);
    EXPECT_EQ(status, ZX_OK);
  }
}

#ifdef __Fuchsia__
TEST(Unaligned, decode_single_present_handle_unaligned_error) {
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

  // Decoding the unaligned version of the struct should fail.
  const char* error = nullptr;
  auto status = fidl_decode_transactional(&nonnullable_handle_message_type, &message,
                                          sizeof(message), handles, ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
}
#endif  //__Fuchsia__

TEST(Unaligned, decode_present_nonnullable_string_unaligned_error) {
  unbounded_nonnullable_string_message_layout message = {};
  message.inline_struct.string = fidl_string_t{6, reinterpret_cast<char*>(FIDL_ALLOC_PRESENT)};
  memcpy(message.data, "hello!", 6);

  // Copy the message to unaligned storage one byte off from true alignment
  unbounded_nonnullable_string_message_layout message_storage[2];
  uint8_t* unaligned_ptr = reinterpret_cast<uint8_t*>(&message_storage[0]) + 1;
  memcpy(unaligned_ptr, &message, sizeof(message));

  const char* error = nullptr;
  auto status = fidl_decode_transactional(&unbounded_nonnullable_string_message_type, unaligned_ptr,
                                          sizeof(message), nullptr, 0, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
  ASSERT_SUBSTR(error, "must be aligned to FIDL_ALIGNMENT");
}

TEST(BufferTooSmall, decode_overflow_buffer_on_FidlAlign) {
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
  auto status = fidl_decode(&type, &message, 1, nullptr, 0, &error);

  // Expect error to be something about buffer too small (for for properly padded message).
  EXPECT_EQ(status, ZX_ERR_BUFFER_TOO_SMALL);
  EXPECT_NOT_NULL(error);
  ASSERT_SUBSTR(error, "too small");
}

#ifdef __Fuchsia__
TEST(Structs, decode_nested_nonnullable_structs) {
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
  auto status = fidl_decode_transactional(&nested_structs_message_type, &message, sizeof(message),
                                          handles, ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  // Note the traversal order! l1 -> l3 -> l2 -> l0
  EXPECT_EQ(message.inline_struct.l0.l1.handle_1, dummy_handle_0);
  EXPECT_EQ(message.inline_struct.l0.l1.l2.l3.handle_3, dummy_handle_1);
  EXPECT_EQ(message.inline_struct.l0.l1.l2.handle_2, dummy_handle_2);
  EXPECT_EQ(message.inline_struct.l0.handle_0, dummy_handle_3);
}

TEST(Structs, decode_nested_nonnullable_structs_check_padding) {
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

    buffer[padding_offset] = 0xAA;

    zx_handle_t handles[] = {
        dummy_handle_0,
        dummy_handle_1,
        dummy_handle_2,
        dummy_handle_3,
    };

    const char* error = nullptr;
    auto status = fidl_decode_transactional(&nested_structs_message_type, &message, kBufferSize,
                                            handles, ArrayCount(handles), &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    ASSERT_NOT_NULL(error);
    EXPECT_STREQ(error, "non-zero padding bytes detected");
  }
}

TEST(Structs, decode_nested_nullable_structs) {
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
  auto status = fidl_decode_transactional(&nested_struct_ptrs_message_type, &message,
                                          sizeof(message), handles, ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);

  // Note the traversal order!

  // 0 inline
  //     1 inline
  //         handle
  EXPECT_EQ(message.inline_struct.l0_inline.l1_inline.handle_1, dummy_handle_0);
  //         2 out of line
  //             3 out of line
  EXPECT_EQ(message.inline_struct.l0_inline.l1_inline.l2_present->l3_present->handle_3,
            dummy_handle_1);
  //             3 inline
  EXPECT_EQ(message.inline_struct.l0_inline.l1_inline.l2_present->l3_inline.handle_3,
            dummy_handle_2);
  //             handle
  EXPECT_EQ(message.inline_struct.l0_inline.l1_inline.l2_present->handle_2, dummy_handle_3);
  //         2 inline
  //             3 out of line
  EXPECT_EQ(message.inline_struct.l0_inline.l1_inline.l2_inline.l3_present->handle_3,
            dummy_handle_4);
  //             3 inline
  EXPECT_EQ(message.inline_struct.l0_inline.l1_inline.l2_inline.l3_inline.handle_3, dummy_handle_5);
  //             handle
  EXPECT_EQ(message.inline_struct.l0_inline.l1_inline.l2_inline.handle_2, dummy_handle_6);
  //     handle
  EXPECT_EQ(message.inline_struct.l0_inline.handle_0, dummy_handle_7);
  //     1 out of line
  //         handle
  EXPECT_EQ(message.inline_struct.l0_inline.l1_present->handle_1, dummy_handle_8);
  //         2 out of line
  //             3 out of line
  EXPECT_EQ(message.inline_struct.l0_inline.l1_present->l2_present->l3_present->handle_3,
            dummy_handle_9);
  //             3 inline
  EXPECT_EQ(message.inline_struct.l0_inline.l1_present->l2_present->l3_inline.handle_3,
            dummy_handle_10);
  //             handle
  EXPECT_EQ(message.inline_struct.l0_inline.l1_present->l2_present->handle_2, dummy_handle_11);
  //         2 inline
  //             3 out of line
  EXPECT_EQ(message.inline_struct.l0_inline.l1_present->l2_inline.l3_present->handle_3,
            dummy_handle_12);
  //             3 inline
  EXPECT_EQ(message.inline_struct.l0_inline.l1_present->l2_inline.l3_inline.handle_3,
            dummy_handle_13);
  //             handle
  EXPECT_EQ(message.inline_struct.l0_inline.l1_present->l2_inline.handle_2, dummy_handle_14);
  // 0 out of line
  //     1 inline
  //         handle
  EXPECT_EQ(message.inline_struct.l0_present->l1_inline.handle_1, dummy_handle_15);
  //         2 out of line
  //             3 out of line
  EXPECT_EQ(message.inline_struct.l0_present->l1_inline.l2_present->l3_present->handle_3,
            dummy_handle_16);
  //             3 inline
  EXPECT_EQ(message.inline_struct.l0_present->l1_inline.l2_present->l3_inline.handle_3,
            dummy_handle_17);
  //             handle
  EXPECT_EQ(message.inline_struct.l0_present->l1_inline.l2_present->handle_2, dummy_handle_18);
  //         2 inline
  //             3 out of line
  EXPECT_EQ(message.inline_struct.l0_present->l1_inline.l2_inline.l3_present->handle_3,
            dummy_handle_19);
  //             3 inline
  EXPECT_EQ(message.inline_struct.l0_present->l1_inline.l2_inline.l3_inline.handle_3,
            dummy_handle_20);
  //             handle
  EXPECT_EQ(message.inline_struct.l0_present->l1_inline.l2_inline.handle_2, dummy_handle_21);
  //     handle
  EXPECT_EQ(message.inline_struct.l0_present->handle_0, dummy_handle_22);
  //     1 out of line
  //         handle
  EXPECT_EQ(message.inline_struct.l0_present->l1_present->handle_1, dummy_handle_23);
  //         2 out of line
  //             3 out of line
  EXPECT_EQ(message.inline_struct.l0_present->l1_present->l2_present->l3_present->handle_3,
            dummy_handle_24);
  //             3 inline
  EXPECT_EQ(message.inline_struct.l0_present->l1_present->l2_present->l3_inline.handle_3,
            dummy_handle_25);
  //             handle
  EXPECT_EQ(message.inline_struct.l0_present->l1_present->l2_present->handle_2, dummy_handle_26);
  //         2 inline
  //             3 out of line
  EXPECT_EQ(message.inline_struct.l0_present->l1_present->l2_inline.l3_present->handle_3,
            dummy_handle_27);
  //             3 inline
  EXPECT_EQ(message.inline_struct.l0_present->l1_present->l2_inline.l3_inline.handle_3,
            dummy_handle_28);
  //             handle
  EXPECT_EQ(message.inline_struct.l0_present->l1_present->l2_inline.handle_2, dummy_handle_29);

  // Finally, check that all absent members are nullptr.
  EXPECT_NULL(message.inline_struct.l0_absent);
  EXPECT_NULL(message.inline_struct.l0_inline.l1_absent);
  EXPECT_NULL(message.inline_struct.l0_inline.l1_inline.l2_absent);
  EXPECT_NULL(message.inline_struct.l0_inline.l1_inline.l2_inline.l3_absent);
  EXPECT_NULL(message.inline_struct.l0_inline.l1_inline.l2_present->l3_absent);
  EXPECT_NULL(message.inline_struct.l0_inline.l1_present->l2_absent);
  EXPECT_NULL(message.inline_struct.l0_inline.l1_present->l2_inline.l3_absent);
  EXPECT_NULL(message.inline_struct.l0_inline.l1_present->l2_present->l3_absent);
  EXPECT_NULL(message.inline_struct.l0_present->l1_absent);
  EXPECT_NULL(message.inline_struct.l0_present->l1_inline.l2_absent);
  EXPECT_NULL(message.inline_struct.l0_present->l1_inline.l2_inline.l3_absent);
  EXPECT_NULL(message.inline_struct.l0_present->l1_inline.l2_present->l3_absent);
  EXPECT_NULL(message.inline_struct.l0_present->l1_present->l2_absent);
  EXPECT_NULL(message.inline_struct.l0_present->l1_present->l2_inline.l3_absent);
  EXPECT_NULL(message.inline_struct.l0_present->l1_present->l2_present->l3_absent);
}

TEST(UnknownEnvelope, NumUnknownHandlesOverflows) {
  uint8_t bytes[] = {
      3,   0,   0,   0,   0,   0,   0,   0,    // field count
      255, 255, 255, 255, 255, 255, 255, 255,  // alloc present

      0,   0,   0,   0,   0,   0,   0,   0,  // envelope 1: num bytes / num handles
      0,   0,   0,   0,   0,   0,   0,   0,  // alloc absent

      0,   0,   0,   0,   1,   0,   0,   0,    // envelope 2: num bytes / num handles
      255, 255, 255, 255, 255, 255, 255, 255,  // alloc present

      0,   0,   0,   0,   255, 255, 255, 255,  // envelope 3: num bytes / num handles
      255, 255, 255, 255, 255, 255, 255, 255,  // alloc present
  };
  zx_handle_t handles[1] = {};

  const char* error = nullptr;
  auto status = fidl_decode(&fidl_test_coding::wire::fidl_test_coding_ResourceSimpleTableTable,
                            bytes, ArrayCount(bytes), handles, ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_STREQ(error, "number of unknown handles overflows");
}

TEST(UnknownEnvelope, NumIncomingHandlesOverflows) {
  uint8_t bytes[] = {
      2,   0,   0,   0,   0,   0,   0,   0,    // field count
      255, 255, 255, 255, 255, 255, 255, 255,  // alloc present

      0,   0,   0,   0,   0,   0,   0,   0,  // envelope 1: num bytes / num handles
      0,   0,   0,   0,   0,   0,   0,   0,  // alloc absent

      0,   0,   0,   0,   1,   0,   0,   0,    // envelope 2: num bytes / num handles
      255, 255, 255, 255, 255, 255, 255, 255,  // alloc present
  };
  zx_handle_t handles[1] = {};

  const char* error = nullptr;
  auto status = fidl_decode(&fidl_test_coding::wire::fidl_test_coding_ResourceSimpleTableTable,
                            bytes, ArrayCount(bytes), handles, 0, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_STREQ(error, "number of incoming handles exceeds incoming handle array size");
}
#endif

#ifdef __Fuchsia__
TEST(UnknownEnvelope, NumUnknownHandlesExceedsUnknownArraySize) {
  uint8_t bytes[] = {
      2,   0,   0,   0,   0,   0,   0,   0,    // field count
      255, 255, 255, 255, 255, 255, 255, 255,  // alloc present

      0,   0,   0,   0,   0,   0,   0,   0,  // envelope 1: num bytes / num handles
      0,   0,   0,   0,   0,   0,   0,   0,  // alloc absent

      0,   0,   0,   0,   65,  0,   0,   0,    // envelope 2: num bytes / num handles
      255, 255, 255, 255, 255, 255, 255, 255,  // alloc present
  };

  const char* error = nullptr;
  auto status = fidl_decode(&fidl_test_coding::wire::fidl_test_coding_ResourceSimpleTableTable,
                            bytes, ArrayCount(bytes), nullptr, 0, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_STREQ(error, "number of unknown handles exceeds unknown handle array size");
}
#endif

#ifdef __Fuchsia__
TEST(UnknownEnvelope, DecodeUnknownHandle) {
  uint8_t bytes[] = {
      2,   0,   0,   0,   0,   0,   0,   0,    // field count
      255, 255, 255, 255, 255, 255, 255, 255,  // alloc present

      0,   0,   0,   0,   0,   0,   0,   0,  // envelope 1: num bytes / num handles
      0,   0,   0,   0,   0,   0,   0,   0,  // alloc present

      0,   0,   0,   0,   1,   0,   0,   0,    // envelope 2: num bytes / num handles
      255, 255, 255, 255, 255, 255, 255, 255,  // alloc present
  };

  zx_handle_t handles[1] = {};
  ASSERT_EQ(ZX_OK, zx_port_create(0, handles));
  const char* error = nullptr;
  auto status = fidl_decode(&fidl_test_coding::wire::fidl_test_coding_SimpleTableTable, bytes,
                            ArrayCount(bytes), handles, 1, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(zx_object_get_info(handles[0], ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr),
            ZX_ERR_BAD_HANDLE);
}

TEST(UnknownEnvelope, DecodeEtcUnknownHandle) {
  uint8_t bytes[] = {
      2,   0,   0,   0,   0,   0,   0,   0,    // max ordinal
      255, 255, 255, 255, 255, 255, 255, 255,  // alloc present

      0,   0,   0,   0,   0,   0,   0,   0,  // envelope 1: num bytes / num handles
      0,   0,   0,   0,   0,   0,   0,   0,  // alloc present

      0,   0,   0,   0,   1,   0,   0,   0,    // envelope 2: num bytes / num handles
      255, 255, 255, 255, 255, 255, 255, 255,  // alloc present
  };

  zx_handle_info_t handles[1] = {zx_handle_info_t{
      .handle = ZX_HANDLE_INVALID,
      .type = ZX_OBJ_TYPE_PORT,
      .rights = ZX_RIGHT_SAME_RIGHTS,
  }};
  ASSERT_EQ(ZX_OK, zx_port_create(0, &handles[0].handle));
  const char* error = nullptr;
  auto status = fidl_decode_etc(&fidl_test_coding::wire::fidl_test_coding_SimpleTableTable, bytes,
                                ArrayCount(bytes), handles, 1, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(
      zx_object_get_info(handles[0].handle, ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr),
      ZX_ERR_BAD_HANDLE);
}

TEST(UnknownEnvelope, DecodeEtcHLCPP) {
  uint8_t bytes[] = {
      2,   0,   0,   0,   0,   0,   0,   0,    // max ordinal
      255, 255, 255, 255, 255, 255, 255, 255,  // alloc present

      0,   0,   0,   0,   0,   0,   0,   0,  // envelope 1: zero

      0,   0,   0,   0,   1,   0,   0,   0,  // envelope 2: num bytes / num handles / not inline
  };

  zx_handle_info_t handles[1] = {zx_handle_info_t{
      .handle = ZX_HANDLE_INVALID,
      .type = ZX_OBJ_TYPE_PORT,
      .rights = ZX_RIGHT_SAME_RIGHTS,
  }};
  ASSERT_EQ(ZX_OK, zx_port_create(0, &handles[0].handle));
  const char* error = nullptr;
  auto status = internal__fidl_decode_etc_hlcpp__v2__may_break(
      &fidl_test_coding::wire::fidl_test_coding_ResourceSimpleTableTable, bytes, ArrayCount(bytes),
      handles, 1, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(
      zx_object_get_info(handles[0].handle, ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr),
      ZX_OK);
  EXPECT_EQ(zx_handle_close(handles[0].handle), ZX_OK);
}

#endif

TEST(UnknownEnvelope, V2DecodeUnknownInlineEnvelope) {
  uint8_t bytes[] = {
      2,   0,   0,   0,   0,   0,   0,   0,    // max ordinal
      255, 255, 255, 255, 255, 255, 255, 255,  // alloc present

      0,   0,   0,   0,   0,   0,   0,   0,  // envelope 1: zero envelope
      123, 0,   0,   0,   0,   0,   1,   0,  // envelope 2: num bytes / num handles / inlined
  };

  const char* error = nullptr;
  auto status = internal_fidl_decode_etc__v2__may_break(
      &fidl_test_coding::wire::fidl_test_coding_SimpleTableTable, bytes, ArrayCount(bytes), nullptr,
      0, &error);

  EXPECT_EQ(status, ZX_OK);

  // Compare the bytes of the last envelope after they are transformed.
  uint8_t expected_decoded_envelope[] = {
      123, 0, 0, 0, 0, 0, 1, 0,  // envelope 2: num bytes / num handles / inlined
  };
  EXPECT_BYTES_EQ(expected_decoded_envelope, bytes + 24, 8);
}

TEST(UnknownEnvelope, V2DecodeUnknownOutOfLineEnvelope) {
  uint8_t bytes[] = {
      2,   0,   0,   0,   0,   0,   0,   0,    // max ordinal
      255, 255, 255, 255, 255, 255, 255, 255,  // alloc present

      0,   0,   0,   0,   0,   0,   0,   0,  // envelope 1: zero envelope
      8,   0,   0,   0,   0,   0,   0,   0,  // envelope 2: num bytes / num handles / inlined
      1,   2,   3,   4,   5,   6,   7,   8,  // out of line data for envelope 2
  };

  const char* error = nullptr;
  auto status = internal__fidl_decode_etc_hlcpp__v2__may_break(
      &fidl_test_coding::wire::fidl_test_coding_SimpleTableTable, bytes, ArrayCount(bytes), nullptr,
      0, &error);

  EXPECT_EQ(status, ZX_OK);

  // Compare the bytes of the last envelope after they are transformed.
  uint8_t expected_decoded_envelope[] = {
      8, 0, 32, 0, 0, 0, 0, 0,  // envelope 2: num bytes / offset
  };
  EXPECT_BYTES_EQ(expected_decoded_envelope, bytes + 24, 8);
}

// Most fidl_encode_etc code paths are covered by the fidl_encode tests.
// The FidlDecodeEtc tests cover additional paths.

#ifdef __Fuchsia__
TEST(FidlDecodeEtc, decode_invalid_handle_info) {
  nonnullable_handle_message_layout message = {};
  message.inline_struct.handle = FIDL_HANDLE_PRESENT;

  zx_handle_info_t handle_infos[] = {{
      .handle = ZX_HANDLE_INVALID,
      .type = ZX_OBJ_TYPE_NONE,
      .rights = 0,
      .unused = 0,
  }};

  const char* error = nullptr;
  auto status =
      fidl_decode_etc_transactional(&nonnullable_handle_message_type, &message, sizeof(message),
                                    handle_infos, ArrayCount(handle_infos), &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  ASSERT_NOT_NULL(error);
  const char expected_error_msg[] = "invalid handle detected in handle table";
  EXPECT_STREQ(expected_error_msg, error, "wrong error msg");
}

TEST(FidlDecodeEtc, decode_single_present_handle_info_handle_rights_subtype_match) {
  nonnullable_handle_message_layout message = {};
  message.inline_struct.handle = FIDL_HANDLE_PRESENT;

  zx_handle_info_t handle_infos[] = {{
      .handle = dummy_handle_0,
      .type = ZX_OBJ_TYPE_CHANNEL,
      .rights = ZX_RIGHT_READ | ZX_RIGHT_WRITE,
      .unused = 0,
  }};

  const char* error = nullptr;
  auto status =
      fidl_decode_etc_transactional(&nonnullable_channel_message_type, &message, sizeof(message),
                                    handle_infos, ArrayCount(handle_infos), &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(message.inline_struct.handle, dummy_handle_0);
}

TEST(FidlDecodeEtc, decode_single_present_handle_info_no_subtype_same_rights) {
  nonnullable_handle_message_layout message = {};
  message.inline_struct.handle = FIDL_HANDLE_PRESENT;

  zx_handle_info_t handle_infos[] = {{
      .handle = dummy_handle_0,
      .type = ZX_OBJ_TYPE_CHANNEL,
      .rights = ZX_RIGHT_READ | ZX_RIGHT_WRITE,
      .unused = 0,
  }};

  const char* error = nullptr;
  auto status =
      fidl_decode_etc_transactional(&nonnullable_handle_message_type, &message, sizeof(message),
                                    handle_infos, ArrayCount(handle_infos), &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(message.inline_struct.handle, dummy_handle_0);
}

TEST(FidlDecodeEtc, decode_single_present_handle_info_handle_rights_wrong_subtype) {
  nonnullable_handle_message_layout message = {};
  message.inline_struct.handle = FIDL_HANDLE_PRESENT;

  zx_handle_info_t handle_infos[] = {{
      .handle = dummy_handle_0,
      .type = ZX_OBJ_TYPE_VMO,
      .rights = ZX_RIGHT_READ | ZX_RIGHT_WRITE,
      .unused = 0,
  }};

  const char* error = nullptr;
  auto status =
      fidl_decode_etc_transactional(&nonnullable_channel_message_type, &message, sizeof(message),
                                    handle_infos, ArrayCount(handle_infos), &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  ASSERT_SUBSTR(error, "object type does not match expected type");
}

TEST(FidlDecodeEtc, decode_single_present_handle_info_handle_rights_missing_required_rights) {
  nonnullable_handle_message_layout message = {};
  message.inline_struct.handle = FIDL_HANDLE_PRESENT;

  zx_handle_info_t handle_infos[] = {{
      .handle = dummy_handle_0,
      .type = ZX_OBJ_TYPE_CHANNEL,
      .rights = ZX_RIGHT_READ,
      .unused = 0,
  }};

  const char* error = nullptr;
  auto status =
      fidl_decode_etc_transactional(&nonnullable_channel_message_type, &message, sizeof(message),
                                    handle_infos, ArrayCount(handle_infos), &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  ASSERT_SUBSTR(error, "required rights");
}

TEST(FidlDecodeEtc, decode_single_present_handle_info_handle_rights_too_many_rights) {
  nonnullable_handle_message_layout message = {};
  message.inline_struct.handle = FIDL_HANDLE_PRESENT;

  zx_handle_t h0, h1;
  auto status = zx_channel_create(0, &h0, &h1);
  ASSERT_EQ(status, ZX_OK);

  zx_handle_info_t handle_infos[] = {{
      .handle = h0,
      .type = ZX_OBJ_TYPE_CHANNEL,
      .rights = ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_TRANSFER,
      .unused = 0,
  }};

  const char* error = nullptr;
  status =
      fidl_decode_etc_transactional(&nonnullable_channel_message_type, &message, sizeof(message),
                                    handle_infos, ArrayCount(handle_infos), &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  // There should be a new handle created by zx_handle_replace.
  EXPECT_NE(message.inline_struct.handle, h0);

  zx_info_handle_basic_t info;
  zx_object_get_info(message.inline_struct.handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info),
                     nullptr, nullptr);
  EXPECT_EQ(info.type, handle_infos[0].type);
  EXPECT_EQ(info.rights, ZX_RIGHT_READ | ZX_RIGHT_WRITE);
}
#endif

}  // namespace
}  // namespace fidl
