// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>
#include <stddef.h>

#include <limits>
#include <memory>

#include <unittest/unittest.h>

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

bool validate_null_validate_parameters() {
  BEGIN_TEST;

  zx_handle_t handles[] = {static_cast<zx_handle_t>(23)};

  // Null message type.
  {
    nonnullable_handle_message_layout message = {};
    message.inline_struct.handle = FIDL_HANDLE_PRESENT;
    const char* error = nullptr;
    auto status = fidl_validate(nullptr, &message, sizeof(nonnullable_handle_message_layout),
                                ArrayCount(handles), &error);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);
  }

  // Null message.
  {
    const char* error = nullptr;
    auto status =
        fidl_validate(&nonnullable_handle_message_type, nullptr,
                      sizeof(nonnullable_handle_message_layout), ArrayCount(handles), &error);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);
  }

  // Zero handles, for a message that has a handle.
  {
    nonnullable_handle_message_layout message = {};
    message.inline_struct.handle = FIDL_HANDLE_PRESENT;
    const char* error = nullptr;
    auto status = fidl_validate(&nonnullable_handle_message_type, &message,
                                sizeof(nonnullable_handle_message_layout), 0, &error);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);
  }

  // A null error string pointer is ok, though.
  {
    auto status = fidl_validate(nullptr, nullptr, 0u, 0u, nullptr);
    EXPECT_NE(status, ZX_OK);
  }

  // A null error is also ok in success cases.
  {
    nonnullable_handle_message_layout message = {};
    message.inline_struct.handle = FIDL_HANDLE_PRESENT;
    auto status =
        fidl_validate(&nonnullable_handle_message_type, &message,
                      sizeof(nonnullable_handle_message_layout), ArrayCount(handles), nullptr);
    EXPECT_EQ(status, ZX_OK);
  }

  END_TEST;
}

bool validate_single_present_handle() {
  BEGIN_TEST;

  nonnullable_handle_message_layout message = {};
  message.inline_struct.handle = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[] = {
      dummy_handle_0,
  };

  const char* error = nullptr;
  auto status = fidl_validate(&nonnullable_handle_message_type, &message, sizeof(message),
                              ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);
  EXPECT_EQ(message.inline_struct.handle, FIDL_HANDLE_PRESENT);

  END_TEST;
}

bool validate_single_present_handle_check_trailing_padding() {
  BEGIN_TEST;

  // There are four padding bytes; any of them not being zero should lead to an error.
  for (size_t i = 0; i < 4; i++) {
    constexpr size_t kBufferSize = sizeof(nonnullable_handle_message_layout);
    nonnullable_handle_message_layout message;
    uint8_t* buffer = reinterpret_cast<uint8_t*>(&message);
    memset(buffer, 0, kBufferSize);
    message.inline_struct.handle = FIDL_HANDLE_PRESENT;
    constexpr uint32_t kNumHandles = 1;

    buffer[kBufferSize - 4 + i] = 0xAA;

    const char* error = nullptr;
    auto status =
        fidl_validate(&nonnullable_handle_message_type, &message, kBufferSize, kNumHandles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_STR_EQ(error, "non-zero padding bytes detected");
  }

  END_TEST;
}

bool validate_too_many_handles_specified_error() {
  BEGIN_TEST;

  nonnullable_handle_message_layout message = {};
  message.inline_struct.handle = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[] = {
      dummy_handle_0,
      ZX_HANDLE_INVALID,
  };

  const char* error = nullptr;
  auto status = fidl_validate(&nonnullable_handle_message_type, &message, sizeof(message),
                              ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);
  EXPECT_EQ(message.inline_struct.handle, FIDL_HANDLE_PRESENT);

  END_TEST;
}

bool validate_single_present_handle_unaligned_error() {
  BEGIN_TEST;

  // Test a short, unaligned version of nonnullable message
  // handle. All fidl message objects should be 8 byte aligned.
  //
  // We use a 16 bytes array rather than fidl_message_header_t to avoid
  // aligning to 8 bytes.
  struct unaligned_nonnullable_handle_inline_data {
    uint8_t header[sizeof(fidl_message_header_t)];
    zx_handle_t handle;
  };
  struct unaligned_nonnullable_handle_message_layout {
    unaligned_nonnullable_handle_inline_data inline_struct;
  };

  unaligned_nonnullable_handle_message_layout message = {};
  message.inline_struct.handle = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[] = {
      dummy_handle_0,
  };

  // Validating the unaligned version of the struct should fail.
  const char* error = nullptr;
  auto status = fidl_validate(&nonnullable_handle_message_type, &message, sizeof(message),
                              ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);

  END_TEST;
}

bool validate_multiple_present_handles() {
  BEGIN_TEST;

  multiple_nonnullable_handles_message_layout message = {};
  message.inline_struct.handle_0 = FIDL_HANDLE_PRESENT;
  message.inline_struct.handle_1 = FIDL_HANDLE_PRESENT;
  message.inline_struct.handle_2 = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[] = {
      dummy_handle_0,
      dummy_handle_1,
      dummy_handle_2,
  };

  const char* error = nullptr;
  auto status = fidl_validate(&multiple_nonnullable_handles_message_type, &message, sizeof(message),
                              ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);
  EXPECT_EQ(message.inline_struct.data_0, 0u);
  EXPECT_EQ(message.inline_struct.handle_0, FIDL_HANDLE_PRESENT);
  EXPECT_EQ(message.inline_struct.data_1, 0u);
  EXPECT_EQ(message.inline_struct.handle_1, FIDL_HANDLE_PRESENT);
  EXPECT_EQ(message.inline_struct.handle_2, FIDL_HANDLE_PRESENT);
  EXPECT_EQ(message.inline_struct.data_2, 0u);

  END_TEST;
}

bool validate_single_absent_handle() {
  BEGIN_TEST;

  nullable_handle_message_layout message = {};
  message.inline_struct.handle = FIDL_HANDLE_ABSENT;

  const char* error = nullptr;
  auto status = fidl_validate(&nullable_handle_message_type, &message, sizeof(message), 0, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);
  EXPECT_EQ(message.inline_struct.handle, FIDL_HANDLE_ABSENT);

  END_TEST;
}

bool validate_multiple_absent_handles() {
  BEGIN_TEST;

  multiple_nullable_handles_message_layout message = {};
  message.inline_struct.handle_0 = FIDL_HANDLE_ABSENT;
  message.inline_struct.handle_1 = FIDL_HANDLE_ABSENT;
  message.inline_struct.handle_2 = FIDL_HANDLE_ABSENT;

  const char* error = nullptr;
  auto status =
      fidl_validate(&multiple_nullable_handles_message_type, &message, sizeof(message), 0, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);
  EXPECT_EQ(message.inline_struct.data_0, 0u);
  EXPECT_EQ(message.inline_struct.handle_0, FIDL_HANDLE_ABSENT);
  EXPECT_EQ(message.inline_struct.data_1, 0u);
  EXPECT_EQ(message.inline_struct.handle_1, FIDL_HANDLE_ABSENT);
  EXPECT_EQ(message.inline_struct.handle_2, FIDL_HANDLE_ABSENT);
  EXPECT_EQ(message.inline_struct.data_2, 0u);

  END_TEST;
}

bool validate_array_of_present_handles() {
  BEGIN_TEST;

  array_of_nonnullable_handles_message_layout message = {};
  message.inline_struct.handles[0] = FIDL_HANDLE_PRESENT;
  message.inline_struct.handles[1] = FIDL_HANDLE_PRESENT;
  message.inline_struct.handles[2] = FIDL_HANDLE_PRESENT;
  message.inline_struct.handles[3] = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[] = {
      dummy_handle_0,
      dummy_handle_1,
      dummy_handle_2,
      dummy_handle_3,
  };

  const char* error = nullptr;
  auto status = fidl_validate(&array_of_nonnullable_handles_message_type, &message, sizeof(message),
                              ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);
  EXPECT_EQ(message.inline_struct.handles[0], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(message.inline_struct.handles[1], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(message.inline_struct.handles[2], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(message.inline_struct.handles[3], FIDL_HANDLE_PRESENT);

  END_TEST;
}

bool validate_array_of_nonnullable_handles_some_absent_error() {
  BEGIN_TEST;

  array_of_nonnullable_handles_message_layout message = {};
  message.inline_struct.handles[0] = FIDL_HANDLE_PRESENT;
  message.inline_struct.handles[1] = FIDL_HANDLE_PRESENT;
  message.inline_struct.handles[2] = FIDL_HANDLE_PRESENT;
  message.inline_struct.handles[3] = FIDL_HANDLE_ABSENT;

  zx_handle_t handles[] = {
      dummy_handle_0,
      dummy_handle_1,
      dummy_handle_2,
      dummy_handle_3,
  };

  const char* error = nullptr;
  auto status = fidl_validate(&array_of_nonnullable_handles_message_type, &message, sizeof(message),
                              ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);

  END_TEST;
}

bool validate_array_of_nullable_handles() {
  BEGIN_TEST;

  array_of_nullable_handles_message_layout message = {};
  message.inline_struct.handles[0] = FIDL_HANDLE_PRESENT;
  message.inline_struct.handles[1] = FIDL_HANDLE_ABSENT;
  message.inline_struct.handles[2] = FIDL_HANDLE_PRESENT;
  message.inline_struct.handles[3] = FIDL_HANDLE_ABSENT;
  message.inline_struct.handles[4] = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[] = {
      dummy_handle_0,
      dummy_handle_1,
      dummy_handle_2,
  };

  const char* error = nullptr;
  auto status = fidl_validate(&array_of_nullable_handles_message_type, &message, sizeof(message),
                              ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);
  EXPECT_EQ(message.inline_struct.handles[0], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(message.inline_struct.handles[1], FIDL_HANDLE_ABSENT);
  EXPECT_EQ(message.inline_struct.handles[2], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(message.inline_struct.handles[3], FIDL_HANDLE_ABSENT);
  EXPECT_EQ(message.inline_struct.handles[4], FIDL_HANDLE_PRESENT);

  END_TEST;
}

bool validate_array_of_nullable_handles_with_insufficient_handles_error() {
  BEGIN_TEST;

  array_of_nullable_handles_message_layout message = {};
  message.inline_struct.handles[0] = FIDL_HANDLE_PRESENT;
  message.inline_struct.handles[1] = FIDL_HANDLE_ABSENT;
  message.inline_struct.handles[2] = FIDL_HANDLE_PRESENT;
  message.inline_struct.handles[3] = FIDL_HANDLE_ABSENT;
  message.inline_struct.handles[4] = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[] = {
      dummy_handle_0,
      dummy_handle_1,
  };

  const char* error = nullptr;
  auto status = fidl_validate(&array_of_nullable_handles_message_type, &message, sizeof(message),
                              ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);

  END_TEST;
}

bool validate_array_of_array_of_present_handles() {
  BEGIN_TEST;

  array_of_array_of_nonnullable_handles_message_layout message = {};
  message.inline_struct.handles[0][0] = FIDL_HANDLE_PRESENT;
  message.inline_struct.handles[0][1] = FIDL_HANDLE_PRESENT;
  message.inline_struct.handles[0][2] = FIDL_HANDLE_PRESENT;
  message.inline_struct.handles[0][3] = FIDL_HANDLE_PRESENT;
  message.inline_struct.handles[1][0] = FIDL_HANDLE_PRESENT;
  message.inline_struct.handles[1][1] = FIDL_HANDLE_PRESENT;
  message.inline_struct.handles[1][2] = FIDL_HANDLE_PRESENT;
  message.inline_struct.handles[1][3] = FIDL_HANDLE_PRESENT;
  message.inline_struct.handles[2][0] = FIDL_HANDLE_PRESENT;
  message.inline_struct.handles[2][1] = FIDL_HANDLE_PRESENT;
  message.inline_struct.handles[2][2] = FIDL_HANDLE_PRESENT;
  message.inline_struct.handles[2][3] = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[] = {
      dummy_handle_0, dummy_handle_1, dummy_handle_2,  dummy_handle_3,
      dummy_handle_4, dummy_handle_5, dummy_handle_6,  dummy_handle_7,
      dummy_handle_8, dummy_handle_9, dummy_handle_10, dummy_handle_11,
  };

  const char* error = nullptr;
  auto status = fidl_validate(&array_of_array_of_nonnullable_handles_message_type, &message,
                              sizeof(message), ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);
  EXPECT_EQ(message.inline_struct.handles[0][0], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(message.inline_struct.handles[0][1], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(message.inline_struct.handles[0][2], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(message.inline_struct.handles[0][3], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(message.inline_struct.handles[1][0], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(message.inline_struct.handles[1][1], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(message.inline_struct.handles[1][2], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(message.inline_struct.handles[1][3], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(message.inline_struct.handles[2][0], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(message.inline_struct.handles[2][1], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(message.inline_struct.handles[2][2], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(message.inline_struct.handles[2][3], FIDL_HANDLE_PRESENT);

  END_TEST;
}

bool validate_out_of_line_array() {
  BEGIN_TEST;

  out_of_line_array_of_nonnullable_handles_message_layout message = {};
  message.inline_struct.maybe_array =
      reinterpret_cast<array_of_nonnullable_handles*>(FIDL_ALLOC_PRESENT);
  message.data.handles[0] = FIDL_HANDLE_PRESENT;
  message.data.handles[1] = FIDL_HANDLE_PRESENT;
  message.data.handles[2] = FIDL_HANDLE_PRESENT;
  message.data.handles[3] = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[] = {
      dummy_handle_0,
      dummy_handle_1,
      dummy_handle_2,
      dummy_handle_3,
  };

  const char* error = nullptr;
  auto status = fidl_validate(&out_of_line_array_of_nonnullable_handles_message_type, &message,
                              sizeof(message), ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  END_TEST;
}

bool validate_present_nonnullable_string() {
  BEGIN_TEST;

  unbounded_nonnullable_string_message_layout message = {};
  message.inline_struct.string = fidl_string_t{6, reinterpret_cast<char*>(FIDL_ALLOC_PRESENT)};
  memcpy(message.data, "hello!", 6);

  const char* error = nullptr;
  auto status = fidl_validate(&unbounded_nonnullable_string_message_type, &message, sizeof(message),
                              0, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  END_TEST;
}

bool validate_present_nullable_string() {
  BEGIN_TEST;

  unbounded_nullable_string_message_layout message = {};
  message.inline_struct.string = fidl_string_t{6, reinterpret_cast<char*>(FIDL_ALLOC_PRESENT)};
  memcpy(message.data, "hello!", 6);

  const char* error = nullptr;
  auto status =
      fidl_validate(&unbounded_nullable_string_message_type, &message, sizeof(message), 0, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  END_TEST;
}

bool validate_multiple_present_nullable_string() {
  BEGIN_TEST;

  // Among other things, this test ensures we handle out-of-line
  // alignment to FIDL_ALIGNMENT (i.e., 8) bytes correctly.
  multiple_nullable_strings_message_layout message;
  memset(&message, 0, sizeof(message));

  message.inline_struct.string = fidl_string_t{6, reinterpret_cast<char*>(FIDL_ALLOC_PRESENT)};
  message.inline_struct.string2 = fidl_string_t{8, reinterpret_cast<char*>(FIDL_ALLOC_PRESENT)};
  memcpy(message.data, "hello ", 6);
  memcpy(message.data2, "world!!! ", 8);

  const char* error = nullptr;
  auto status =
      fidl_validate(&multiple_nullable_strings_message_type, &message, sizeof(message), 0, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  END_TEST;
}

bool validate_absent_nonnullable_string_error() {
  BEGIN_TEST;

  unbounded_nonnullable_string_message_layout message = {};
  message.inline_struct.string = fidl_string_t{6, reinterpret_cast<char*>(FIDL_ALLOC_ABSENT)};

  const char* error = nullptr;
  auto status = fidl_validate(&unbounded_nonnullable_string_message_type, &message, sizeof(message),
                              0, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);

  END_TEST;
}

bool validate_absent_nullable_string() {
  BEGIN_TEST;

  unbounded_nullable_string_message_layout message = {};
  message.inline_struct.string = fidl_string_t{0, reinterpret_cast<char*>(FIDL_ALLOC_ABSENT)};

  const char* error = nullptr;
  auto status = fidl_validate(&unbounded_nullable_string_message_type, &message,
                              sizeof(message.inline_struct), 0, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  END_TEST;
}

bool validate_present_nonnullable_bounded_string() {
  BEGIN_TEST;

  bounded_32_nonnullable_string_message_layout message = {};
  message.inline_struct.string = fidl_string_t{6, reinterpret_cast<char*>(FIDL_ALLOC_PRESENT)};
  memcpy(message.data, "hello!", 6);

  const char* error = nullptr;
  auto status = fidl_validate(&bounded_32_nonnullable_string_message_type, &message,
                              sizeof(message), 0, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  END_TEST;
}

bool validate_present_nullable_bounded_string() {
  BEGIN_TEST;

  bounded_32_nullable_string_message_layout message = {};
  message.inline_struct.string = fidl_string_t{6, reinterpret_cast<char*>(FIDL_ALLOC_PRESENT)};
  memcpy(message.data, "hello!", 6);

  const char* error = nullptr;
  auto status =
      fidl_validate(&bounded_32_nullable_string_message_type, &message, sizeof(message), 0, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  END_TEST;
}

bool validate_absent_nonnullable_bounded_string_error() {
  BEGIN_TEST;

  bounded_32_nonnullable_string_message_layout message = {};
  message.inline_struct.string = fidl_string_t{6, reinterpret_cast<char*>(FIDL_ALLOC_ABSENT)};

  const char* error = nullptr;
  auto status = fidl_validate(&bounded_32_nonnullable_string_message_type, &message,
                              sizeof(message), 0, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);

  END_TEST;
}

bool validate_absent_nullable_bounded_string() {
  BEGIN_TEST;

  bounded_32_nullable_string_message_layout message = {};
  message.inline_struct.string = fidl_string_t{0, reinterpret_cast<char*>(FIDL_ALLOC_ABSENT)};

  const char* error = nullptr;
  auto status = fidl_validate(&bounded_32_nullable_string_message_type, &message,
                              sizeof(message.inline_struct), 0, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  END_TEST;
}

bool validate_present_nonnullable_bounded_string_short_error() {
  BEGIN_TEST;

  multiple_short_nonnullable_strings_message_layout message = {};
  message.inline_struct.string = fidl_string_t{6, reinterpret_cast<char*>(FIDL_ALLOC_PRESENT)};
  message.inline_struct.string2 = fidl_string_t{8, reinterpret_cast<char*>(FIDL_ALLOC_PRESENT)};
  memcpy(message.data, "hello ", 6);
  memcpy(message.data2, "world! ", 6);

  const char* error = nullptr;
  auto status = fidl_validate(&multiple_short_nonnullable_strings_message_type, &message,
                              sizeof(message), 0, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);

  END_TEST;
}

bool validate_present_nullable_bounded_string_short_error() {
  BEGIN_TEST;

  multiple_short_nullable_strings_message_layout message = {};
  message.inline_struct.string = fidl_string_t{6, reinterpret_cast<char*>(FIDL_ALLOC_PRESENT)};
  message.inline_struct.string2 = fidl_string_t{8, reinterpret_cast<char*>(FIDL_ALLOC_PRESENT)};
  memcpy(message.data, "hello ", 6);
  memcpy(message.data2, "world! ", 6);

  const char* error = nullptr;
  auto status = fidl_validate(&multiple_short_nullable_strings_message_type, &message,
                              sizeof(message), 0, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);

  END_TEST;
}

bool validate_vector_with_huge_count() {
  BEGIN_TEST;

  unbounded_nonnullable_vector_of_uint32_message_layout message = {};
  // (2^30 + 4) * 4 (4 == sizeof(uint32_t)) overflows to 16 when stored as uint32_t.
  // We want 16 because it happens to be the actual size of the vector data in the message,
  // so we can trigger the overflow without triggering the "tried to claim too many bytes" or
  // "didn't use all the bytes in the message" errors.
  message.inline_struct.vector =
      fidl_vector_t{(1ull << 30) + 4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};

  const char* error = nullptr;
  auto status = fidl_validate(&unbounded_nonnullable_vector_of_uint32_message_type, &message,
                              sizeof(message), 0, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);
  const char expected_error_msg[] = "integer overflow calculating vector size";
  EXPECT_STR_EQ(expected_error_msg, error, "wrong error msg");

  END_TEST;
}

bool validate_present_nonnullable_vector_of_handles() {
  BEGIN_TEST;

  unbounded_nonnullable_vector_of_handles_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};
  message.handles[0] = FIDL_HANDLE_PRESENT;
  message.handles[1] = FIDL_HANDLE_PRESENT;
  message.handles[2] = FIDL_HANDLE_PRESENT;
  message.handles[3] = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[] = {
      dummy_handle_0,
      dummy_handle_1,
      dummy_handle_2,
      dummy_handle_3,
  };

  const char* error = nullptr;
  auto status = fidl_validate(&unbounded_nonnullable_vector_of_handles_message_type, &message,
                              sizeof(message), ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  END_TEST;
}

bool validate_present_nullable_vector_of_handles() {
  BEGIN_TEST;

  unbounded_nullable_vector_of_handles_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};
  message.handles[0] = FIDL_HANDLE_PRESENT;
  message.handles[1] = FIDL_HANDLE_PRESENT;
  message.handles[2] = FIDL_HANDLE_PRESENT;
  message.handles[3] = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[] = {
      dummy_handle_0,
      dummy_handle_1,
      dummy_handle_2,
      dummy_handle_3,
  };

  const char* error = nullptr;
  auto status = fidl_validate(&unbounded_nullable_vector_of_handles_message_type, &message,
                              sizeof(message), ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  END_TEST;
}

bool validate_absent_nonnullable_vector_of_handles_error() {
  BEGIN_TEST;

  unbounded_nonnullable_vector_of_handles_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_ABSENT)};

  zx_handle_t handles[] = {
      dummy_handle_0,
      dummy_handle_1,
      dummy_handle_2,
      dummy_handle_3,
  };

  const char* error = nullptr;
  auto status = fidl_validate(&unbounded_nonnullable_vector_of_handles_message_type, &message,
                              sizeof(message), ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);

  END_TEST;
}

bool validate_absent_nullable_vector_of_handles() {
  BEGIN_TEST;

  unbounded_nullable_vector_of_handles_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{0, reinterpret_cast<void*>(FIDL_ALLOC_ABSENT)};

  const char* error = nullptr;
  auto status = fidl_validate(&unbounded_nullable_vector_of_handles_message_type, &message,
                              sizeof(message.inline_struct), 0u, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  END_TEST;
}

bool validate_present_nonnullable_bounded_vector_of_handles() {
  BEGIN_TEST;

  bounded_32_nonnullable_vector_of_handles_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};
  message.handles[0] = FIDL_HANDLE_PRESENT;
  message.handles[1] = FIDL_HANDLE_PRESENT;
  message.handles[2] = FIDL_HANDLE_PRESENT;
  message.handles[3] = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[] = {
      dummy_handle_0,
      dummy_handle_1,
      dummy_handle_2,
      dummy_handle_3,
  };

  const char* error = nullptr;
  auto status = fidl_validate(&bounded_32_nonnullable_vector_of_handles_message_type, &message,
                              sizeof(message), ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  END_TEST;
}

bool validate_present_nullable_bounded_vector_of_handles() {
  BEGIN_TEST;

  bounded_32_nullable_vector_of_handles_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};
  message.handles[0] = FIDL_HANDLE_PRESENT;
  message.handles[1] = FIDL_HANDLE_PRESENT;
  message.handles[2] = FIDL_HANDLE_PRESENT;
  message.handles[3] = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[] = {
      dummy_handle_0,
      dummy_handle_1,
      dummy_handle_2,
      dummy_handle_3,
  };

  const char* error = nullptr;
  auto status = fidl_validate(&bounded_32_nullable_vector_of_handles_message_type, &message,
                              sizeof(message), ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  END_TEST;
}

bool validate_absent_nonnullable_bounded_vector_of_handles() {
  BEGIN_TEST;

  bounded_32_nonnullable_vector_of_handles_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_ABSENT)};

  const char* error = nullptr;
  auto status = fidl_validate(&bounded_32_nonnullable_vector_of_handles_message_type, &message,
                              sizeof(message.inline_struct), 0u, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);

  END_TEST;
}

bool validate_absent_nullable_bounded_vector_of_handles() {
  BEGIN_TEST;

  bounded_32_nullable_vector_of_handles_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{0, reinterpret_cast<void*>(FIDL_ALLOC_ABSENT)};

  const char* error = nullptr;
  auto status = fidl_validate(&bounded_32_nullable_vector_of_handles_message_type, &message,
                              sizeof(message.inline_struct), 0u, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  END_TEST;
}

bool validate_present_nonnullable_bounded_vector_of_handles_short_error() {
  BEGIN_TEST;

  multiple_nonnullable_vectors_of_handles_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};
  message.inline_struct.vector2 = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};
  message.handles[0] = FIDL_HANDLE_PRESENT;
  message.handles[1] = FIDL_HANDLE_PRESENT;
  message.handles[2] = FIDL_HANDLE_PRESENT;
  message.handles[3] = FIDL_HANDLE_PRESENT;
  message.handles2[0] = FIDL_HANDLE_PRESENT;
  message.handles2[1] = FIDL_HANDLE_PRESENT;
  message.handles2[2] = FIDL_HANDLE_PRESENT;
  message.handles2[3] = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[] = {
      dummy_handle_0, dummy_handle_1, dummy_handle_2, dummy_handle_3,
      dummy_handle_4, dummy_handle_5, dummy_handle_6, dummy_handle_7,
  };

  const char* error = nullptr;
  auto status = fidl_validate(&multiple_nonnullable_vectors_of_handles_message_type, &message,
                              sizeof(message), ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);

  END_TEST;
}

bool validate_present_nullable_bounded_vector_of_handles_short_error() {
  BEGIN_TEST;

  multiple_nullable_vectors_of_handles_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};
  message.inline_struct.vector2 = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};
  message.handles[0] = FIDL_HANDLE_PRESENT;
  message.handles[1] = FIDL_HANDLE_PRESENT;
  message.handles[2] = FIDL_HANDLE_PRESENT;
  message.handles[3] = FIDL_HANDLE_PRESENT;
  message.handles2[0] = FIDL_HANDLE_PRESENT;
  message.handles2[1] = FIDL_HANDLE_PRESENT;
  message.handles2[2] = FIDL_HANDLE_PRESENT;
  message.handles2[3] = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[] = {
      dummy_handle_0, dummy_handle_1, dummy_handle_2, dummy_handle_3,
      dummy_handle_4, dummy_handle_5, dummy_handle_6, dummy_handle_7,
  };

  const char* error = nullptr;
  auto status = fidl_validate(&multiple_nullable_vectors_of_handles_message_type, &message,
                              sizeof(message), ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);

  END_TEST;
}

bool validate_present_nonnullable_vector_of_uint32() {
  BEGIN_TEST;

  unbounded_nonnullable_vector_of_uint32_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};

  const char* error = nullptr;
  auto status = fidl_validate(&unbounded_nonnullable_vector_of_uint32_message_type, &message,
                              sizeof(message), 0, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  auto message_uint32 = reinterpret_cast<zx_handle_t*>(message.inline_struct.vector.data);
  EXPECT_NONNULL(message_uint32);

  END_TEST;
}

bool validate_present_nullable_vector_of_uint32() {
  BEGIN_TEST;

  unbounded_nullable_vector_of_uint32_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};

  const char* error = nullptr;
  auto status = fidl_validate(&unbounded_nullable_vector_of_uint32_message_type, &message,
                              sizeof(message), 0, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  auto message_uint32 = reinterpret_cast<zx_handle_t*>(message.inline_struct.vector.data);
  EXPECT_NONNULL(message_uint32);

  END_TEST;
}

bool validate_absent_nonnullable_vector_of_uint32_error() {
  BEGIN_TEST;

  unbounded_nonnullable_vector_of_uint32_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_ABSENT)};

  const char* error = nullptr;
  auto status = fidl_validate(&unbounded_nonnullable_vector_of_uint32_message_type, &message,
                              sizeof(message), 0, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);

  END_TEST;
}

bool validate_absent_nullable_vector_of_uint32() {
  BEGIN_TEST;

  unbounded_nullable_vector_of_uint32_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{0, reinterpret_cast<void*>(FIDL_ALLOC_ABSENT)};

  const char* error = nullptr;
  auto status = fidl_validate(&unbounded_nullable_vector_of_uint32_message_type, &message,
                              sizeof(message.inline_struct), 0u, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  auto message_uint32 = reinterpret_cast<zx_handle_t*>(message.inline_struct.vector.data);
  EXPECT_NULL(message_uint32);

  END_TEST;
}

bool validate_present_nonnullable_bounded_vector_of_uint32() {
  BEGIN_TEST;

  bounded_32_nonnullable_vector_of_uint32_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};

  const char* error = nullptr;
  auto status = fidl_validate(&bounded_32_nonnullable_vector_of_uint32_message_type, &message,
                              sizeof(message), 0, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  auto message_uint32 = reinterpret_cast<zx_handle_t*>(message.inline_struct.vector.data);
  EXPECT_NONNULL(message_uint32);

  END_TEST;
}

bool validate_present_nullable_bounded_vector_of_uint32() {
  BEGIN_TEST;

  bounded_32_nullable_vector_of_uint32_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};

  const char* error = nullptr;
  auto status = fidl_validate(&bounded_32_nullable_vector_of_uint32_message_type, &message,
                              sizeof(message), 0, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  auto message_uint32 = reinterpret_cast<zx_handle_t*>(message.inline_struct.vector.data);
  EXPECT_NONNULL(message_uint32);

  END_TEST;
}

bool validate_absent_nonnullable_bounded_vector_of_uint32() {
  BEGIN_TEST;

  bounded_32_nonnullable_vector_of_uint32_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_ABSENT)};

  const char* error = nullptr;
  auto status = fidl_validate(&bounded_32_nonnullable_vector_of_uint32_message_type, &message,
                              sizeof(message.inline_struct), 0u, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);

  auto message_uint32 = reinterpret_cast<zx_handle_t*>(message.inline_struct.vector.data);
  EXPECT_NULL(message_uint32);

  END_TEST;
}

bool validate_absent_nullable_bounded_vector_of_uint32() {
  BEGIN_TEST;

  bounded_32_nullable_vector_of_uint32_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{0, reinterpret_cast<void*>(FIDL_ALLOC_ABSENT)};

  const char* error = nullptr;
  auto status = fidl_validate(&bounded_32_nullable_vector_of_uint32_message_type, &message,
                              sizeof(message.inline_struct), 0u, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  auto message_uint32 = reinterpret_cast<zx_handle_t*>(message.inline_struct.vector.data);
  EXPECT_NULL(message_uint32);

  END_TEST;
}

bool validate_present_nonnullable_bounded_vector_of_uint32_short_error() {
  BEGIN_TEST;

  multiple_nonnullable_vectors_of_uint32_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};
  message.inline_struct.vector2 = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};

  const char* error = nullptr;
  auto status = fidl_validate(&multiple_nonnullable_vectors_of_uint32_message_type, &message,
                              sizeof(message), 0, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);

  END_TEST;
}

bool validate_present_nullable_bounded_vector_of_uint32_short_error() {
  BEGIN_TEST;

  multiple_nullable_vectors_of_uint32_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};
  message.inline_struct.vector2 = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};

  const char* error = nullptr;
  auto status = fidl_validate(&multiple_nullable_vectors_of_uint32_message_type, &message,
                              sizeof(message), 0, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);

  END_TEST;
}

bool validate_bad_tagged_union_error() {
  BEGIN_TEST;

  nonnullable_handle_union_message_layout message = {};
  message.inline_struct.data.tag = 43u;
  message.inline_struct.data.handle = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[] = {
      dummy_handle_0,
  };

  const char* error = nullptr;
  auto status = fidl_validate(&nonnullable_handle_union_message_type, &message, sizeof(message),
                              ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);

  END_TEST;
}

bool validate_single_membered_present_nonnullable_union() {
  BEGIN_TEST;

  nonnullable_handle_union_message_layout message = {};
  message.inline_struct.data.tag = nonnullable_handle_union_kHandle;
  message.inline_struct.data.handle = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[] = {
      dummy_handle_0,
  };

  const char* error = nullptr;
  auto status = fidl_validate(&nonnullable_handle_union_message_type, &message, sizeof(message),
                              ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);
  EXPECT_EQ(message.inline_struct.data.tag, nonnullable_handle_union_kHandle);
  EXPECT_EQ(message.inline_struct.data.handle, FIDL_HANDLE_PRESENT);

  END_TEST;
}

bool validate_many_membered_present_nonnullable_union() {
  BEGIN_TEST;

  array_of_nonnullable_handles_union_message_layout message;
  memset(&message, 0, sizeof(message));

  message.inline_struct.data.tag = array_of_nonnullable_handles_union_kArrayOfArrayOfHandles;
  message.inline_struct.data.array_of_array_of_handles[0][0] = FIDL_HANDLE_PRESENT;
  message.inline_struct.data.array_of_array_of_handles[0][1] = FIDL_HANDLE_PRESENT;
  message.inline_struct.data.array_of_array_of_handles[1][0] = FIDL_HANDLE_PRESENT;
  message.inline_struct.data.array_of_array_of_handles[1][1] = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[] = {
      dummy_handle_0,
      dummy_handle_1,
      dummy_handle_2,
      dummy_handle_3,
  };

  const char* error = nullptr;
  auto status = fidl_validate(&array_of_nonnullable_handles_union_message_type, &message,
                              sizeof(message), ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  END_TEST;
}

bool validate_many_membered_present_nonnullable_union_check_padding() {
  BEGIN_TEST;

  // 4 bytes tag + 16 bytes largest variant + 4 bytes padding = 24
  constexpr size_t kUnionSize = 24;
  static_assert(sizeof(array_of_nonnullable_handles_union_message_layout::inline_struct.data) ==
                kUnionSize);
  // The union comes after the 16 byte message header.
  constexpr size_t kUnionOffset = 16;
  // 4 bytes tag
  constexpr size_t kHandleOffset = 4;

  // Any single padding byte being non-zero should result in an error.
  for (size_t i = kHandleOffset + sizeof(zx_handle_t); i < kUnionSize; i++) {
    constexpr size_t kBufferSize = sizeof(array_of_nonnullable_handles_union_message_layout);
    array_of_nonnullable_handles_union_message_layout message;
    uint8_t* buffer = reinterpret_cast<uint8_t*>(&message);
    memset(buffer, 0, kBufferSize);

    ASSERT_EQ(reinterpret_cast<uint8_t*>(&message.inline_struct.data) -
                  reinterpret_cast<uint8_t*>(&message),
              kUnionOffset);
    ASSERT_EQ(reinterpret_cast<uint8_t*>(&message.inline_struct.data.handle) -
                  reinterpret_cast<uint8_t*>(&message.inline_struct.data),
              kHandleOffset);

    message.inline_struct.data.tag = array_of_nonnullable_handles_union_kHandle;
    message.inline_struct.data.handle = FIDL_HANDLE_PRESENT;
    constexpr uint32_t kNumHandles = 1;

    buffer[kUnionOffset + i] = 0xAA;

    const char* error = nullptr;
    auto status = fidl_validate(&array_of_nonnullable_handles_union_message_type, &message,
                                kBufferSize, kNumHandles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_STR_EQ(error, "non-zero padding bytes detected");
  }

  END_TEST;
}

bool validate_single_membered_present_nullable_union() {
  BEGIN_TEST;

  nonnullable_handle_union_ptr_message_layout message = {};
  message.inline_struct.data = reinterpret_cast<nonnullable_handle_union*>(FIDL_ALLOC_PRESENT);
  message.data.tag = nonnullable_handle_union_kHandle;
  message.data.handle = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[] = {
      dummy_handle_0,
  };

  const char* error = nullptr;
  auto status = fidl_validate(&nonnullable_handle_union_ptr_message_type, &message, sizeof(message),
                              ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  END_TEST;
}

bool validate_many_membered_present_nullable_union() {
  BEGIN_TEST;

  array_of_nonnullable_handles_union_ptr_message_layout message;
  memset(&message, 0, sizeof(message));

  message.inline_struct.data =
      reinterpret_cast<array_of_nonnullable_handles_union*>(FIDL_ALLOC_PRESENT);
  message.data.tag = array_of_nonnullable_handles_union_kArrayOfArrayOfHandles;
  message.data.array_of_array_of_handles[0][0] = FIDL_HANDLE_PRESENT;
  message.data.array_of_array_of_handles[0][1] = FIDL_HANDLE_PRESENT;
  message.data.array_of_array_of_handles[1][0] = FIDL_HANDLE_PRESENT;
  message.data.array_of_array_of_handles[1][1] = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[] = {
      dummy_handle_0,
      dummy_handle_1,
      dummy_handle_2,
      dummy_handle_3,
  };

  const char* error = nullptr;
  auto status = fidl_validate(&array_of_nonnullable_handles_union_ptr_message_type, &message,
                              sizeof(message), ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  END_TEST;
}

bool validate_single_membered_absent_nullable_union() {
  BEGIN_TEST;

  nonnullable_handle_union_ptr_message_layout message = {};
  message.inline_struct.data = reinterpret_cast<nonnullable_handle_union*>(FIDL_ALLOC_ABSENT);

  const char* error = nullptr;
  auto status = fidl_validate(&nonnullable_handle_union_ptr_message_type, &message,
                              sizeof(message.inline_struct), 0u, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);
  EXPECT_NULL(message.inline_struct.data);

  END_TEST;
}

bool validate_many_membered_absent_nullable_union() {
  BEGIN_TEST;

  array_of_nonnullable_handles_union_ptr_message_layout message = {};
  message.inline_struct.data =
      reinterpret_cast<array_of_nonnullable_handles_union*>(FIDL_ALLOC_ABSENT);

  const char* error = nullptr;
  auto status = fidl_validate(&array_of_nonnullable_handles_union_ptr_message_type, &message,
                              sizeof(message.inline_struct), 0u, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);
  EXPECT_NULL(message.inline_struct.data);

  END_TEST;
}

bool validate_nested_nonnullable_structs() {
  BEGIN_TEST;

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
  auto status = fidl_validate(&nested_structs_message_type, &message, sizeof(message),
                              ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);
  // Note the traversal order! l1 -> l3 -> l2 -> l0
  EXPECT_EQ(message.inline_struct.l0.l1.handle_1, FIDL_HANDLE_PRESENT);
  EXPECT_EQ(message.inline_struct.l0.l1.l2.l3.handle_3, FIDL_HANDLE_PRESENT);
  EXPECT_EQ(message.inline_struct.l0.l1.l2.handle_2, FIDL_HANDLE_PRESENT);
  EXPECT_EQ(message.inline_struct.l0.handle_0, FIDL_HANDLE_PRESENT);

  END_TEST;
}

bool validate_nested_nonnullable_structs_check_padding() {
  BEGIN_TEST;

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
    auto status =
        fidl_validate(&nested_structs_message_type, &message, kBufferSize, kNumHandles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_STR_EQ(error, "non-zero padding bytes detected");
  }

  END_TEST;
}

bool validate_nested_nullable_structs() {
  BEGIN_TEST;

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
  auto status = fidl_validate(&nested_struct_ptrs_message_type, &message, sizeof(message),
                              ArrayCount(handles), &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  END_TEST;
}

void SetUpRecursionMessage(recursion_message_layout* message) {
  message->inline_struct.inline_union.tag = maybe_recurse_union_kMore;
  message->inline_struct.inline_union.more =
      reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_0.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_0.inline_union.more = reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_1.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_1.inline_union.more = reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_2.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_2.inline_union.more = reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_3.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_3.inline_union.more = reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_4.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_4.inline_union.more = reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_5.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_5.inline_union.more = reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_6.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_6.inline_union.more = reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_7.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_7.inline_union.more = reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_8.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_8.inline_union.more = reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_9.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_9.inline_union.more = reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_10.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_10.inline_union.more =
      reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_11.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_11.inline_union.more =
      reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_12.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_12.inline_union.more =
      reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_13.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_13.inline_union.more =
      reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_14.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_14.inline_union.more =
      reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_15.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_15.inline_union.more =
      reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_16.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_16.inline_union.more =
      reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_17.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_17.inline_union.more =
      reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_18.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_18.inline_union.more =
      reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_19.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_19.inline_union.more =
      reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_20.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_20.inline_union.more =
      reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_21.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_21.inline_union.more =
      reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_22.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_22.inline_union.more =
      reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_23.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_23.inline_union.more =
      reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_24.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_24.inline_union.more =
      reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_25.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_25.inline_union.more =
      reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_26.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_26.inline_union.more =
      reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message->depth_27.inline_union.tag = maybe_recurse_union_kMore;
  message->depth_27.inline_union.more =
      reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
}

bool validate_nested_struct_recursion_too_deep_error() {
  BEGIN_TEST;

  recursion_message_layout message;
  memset(&message, 0, sizeof(message));

  // First we check that FIDL_RECURSION_DEPTH - 1 levels of recursion is OK.
  SetUpRecursionMessage(&message);
  message.depth_28.inline_union.tag = maybe_recurse_union_kDone;
  message.depth_28.inline_union.handle = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[] = {
      dummy_handle_0,
  };

  const char* error = nullptr;
  auto status =
      fidl_validate(&recursion_message_type, &message,
                    // Tell it to ignore everything after we stop recursion.
                    offsetof(recursion_message_layout, depth_29), ArrayCount(handles), &error);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  // Now add another level of recursion.
  SetUpRecursionMessage(&message);
  message.depth_28.inline_union.tag = maybe_recurse_union_kMore;
  message.depth_28.inline_union.more = reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
  message.depth_29.inline_union.tag = maybe_recurse_union_kDone;
  message.depth_29.inline_union.handle = FIDL_HANDLE_PRESENT;

  error = nullptr;
  status = fidl_validate(&recursion_message_type, &message, sizeof(message), ArrayCount(handles),
                         &error);
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);
  const char expected_error_msg[] = "recursion depth exceeded processing struct";
  EXPECT_STR_EQ(expected_error_msg, error, "wrong error msg");

  END_TEST;
}

bool validate_valid_empty_nullable_xunion() {
  BEGIN_TEST;

  SampleNullableXUnionStruct message = {};

  const char* error = nullptr;
  auto status = fidl_validate(&fidl_test_coding_SampleNullableXUnionStructTable, &message,
                              sizeof(fidl_xunion_t), 0, &error);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  END_TEST;
}

bool validate_empty_nonnullable_xunion() {
  BEGIN_TEST;

  SampleXUnionStruct message = {};

  const char* error = nullptr;
  auto status = fidl_validate(&fidl_test_coding_SampleXUnionStructTable, &message,
                              sizeof(fidl_xunion_t), 0, &error);
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);
  EXPECT_STR_EQ(error, "non-nullable xunion is absent");

  END_TEST;
}

bool validate_empty_nullable_xunion_nonzero_ordinal() {
  BEGIN_TEST;

  SampleNullableXUnionStruct message = {};
  message.opt_xu.header.tag = kSampleXUnionIntStructOrdinal;

  const char* error = nullptr;
  auto status = fidl_validate(&fidl_test_coding_SampleNullableXUnionStructTable, &message,
                              sizeof(fidl_xunion_t), 0, &error);
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);
  EXPECT_STR_EQ(error, "empty xunion must have zero as ordinal");

  END_TEST;
}

bool validate_nonempty_xunion_zero_ordinal() {
  BEGIN_TEST;

  SampleXUnionStruct message = {};
  message.xu.header.envelope =
      (fidl_envelope_t){.num_bytes = 8, .num_handles = 0, .presence = FIDL_ALLOC_PRESENT};

  const char* error = nullptr;
  auto status = fidl_validate(&fidl_test_coding_SampleXUnionStructTable, &message,
                              sizeof(SampleXUnionStruct), 0, &error);
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);
  EXPECT_STR_EQ(error, "xunion with zero as ordinal must be empty");

  END_TEST;
}

bool validate_nonempty_nullable_xunion_zero_ordinal() {
  BEGIN_TEST;

  SampleNullableXUnionStruct message = {};
  message.opt_xu.header.envelope =
      (fidl_envelope_t){.num_bytes = 8, .num_handles = 0, .presence = FIDL_ALLOC_PRESENT};

  const char* error = nullptr;
  auto status = fidl_validate(&fidl_test_coding_SampleNullableXUnionStructTable, &message,
                              sizeof(SampleNullableXUnionStruct), 0, &error);
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);
  EXPECT_STR_EQ(error, "xunion with zero as ordinal must be empty");

  END_TEST;
}

bool validate_strict_xunion_unknown_ordinal() {
  BEGIN_TEST;

  uint8_t bytes[] = {
      0xf0, 0x05, 0xc1, 0x0a,                          // invalid ordinal
      0x00, 0x00, 0x00, 0x00,                          // padding
      0x08, 0x00, 0x00, 0x00,                          // envelope: # of bytes
      0x00, 0x00, 0x00, 0x00,                          // envelope: # of handles
      0xff, 0xff, 0xff, 0xff,                          // envelope: data is present
      0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00,  // fake out-of-line data
      0x00, 0x00, 0x00, 0x00,
  };

  const char* error = nullptr;
  auto status = fidl_validate(&fidl_test_coding_SampleStrictXUnionStructTable, bytes, sizeof(bytes),
                              0, &error);
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);
  EXPECT_STR_EQ(error, "strict xunion has unknown ordinal");

  END_TEST;
}

bool validate_flexible_xunion_unknown_ordinal() {
  BEGIN_TEST;

  uint8_t bytes[] = {
      0xf0, 0x05, 0xc1, 0x0a,                          // invalid ordinal
      0x00, 0x00, 0x00, 0x00,                          // padding
      0x08, 0x00, 0x00, 0x00,                          // envelope: # of bytes
      0x00, 0x00, 0x00, 0x00,                          // envelope: # of handles
      0xff, 0xff, 0xff, 0xff,                          // envelope: data is present
      0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00,  // fake out-of-line data
      0x00, 0x00, 0x00, 0x00,
  };

  const char* error = nullptr;
  auto status =
      fidl_validate(&fidl_test_coding_SampleXUnionStructTable, bytes, sizeof(bytes), 0, &error);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error);

  END_TEST;
}

bool validate_zero_16bit_bits() {
  BEGIN_TEST;

  Int16Bits message{.bits = 0};

  const char* error = nullptr;
  auto status =
      fidl_validate(&fidl_test_coding_Int16BitsStructTable, &message, sizeof(message), 0, &error);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  END_TEST;
}

bool validate_valid_16bit_bits() {
  BEGIN_TEST;

  Int16Bits message{.bits = 1u | 16u};

  const char* error = nullptr;
  auto status =
      fidl_validate(&fidl_test_coding_Int16BitsStructTable, &message, sizeof(message), 0, &error);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  END_TEST;
}

bool validate_invalid_16bit_bits() {
  BEGIN_TEST;

  Int16Bits message{.bits = 1u << 7u};

  const char* error = nullptr;
  auto status =
      fidl_validate(&fidl_test_coding_Int16BitsStructTable, &message, sizeof(message), 0, &error);
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_STR_EQ(error, "not a valid bits member");

  END_TEST;
}

bool validate_zero_32bit_bits() {
  BEGIN_TEST;

  Int32Bits message{.bits = 0};

  const char* error = nullptr;
  auto status =
      fidl_validate(&fidl_test_coding_Int32BitsStructTable, &message, sizeof(message), 0, &error);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  END_TEST;
}

bool validate_valid_32bit_bits() {
  BEGIN_TEST;

  // The valid bits are position 7, 12, and 27.
  Int32Bits message{.bits = (1u << 6u) | (1u << 11u) | (1u << 26u)};

  const char* error = nullptr;
  auto status =
      fidl_validate(&fidl_test_coding_Int32BitsStructTable, &message, sizeof(message), 0, &error);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  END_TEST;
}

bool validate_invalid_32bit_bits() {
  BEGIN_TEST;

  // The valid bits are position 7, 12, and 27.
  Int32Bits message{.bits = 1u};

  const char* error = nullptr;
  auto status =
      fidl_validate(&fidl_test_coding_Int32BitsStructTable, &message, sizeof(message), 0, &error);
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_STR_EQ(error, "not a valid bits member");

  END_TEST;
}

template <typename T>
bool TestValidEnum(const fidl_type_t* coding_table) {
  BEGIN_HELPER;

  // See extra_messages.test.fidl for the list of valid members.
  using Underlying = decltype(T::e);
  for (const Underlying valid_value : {
           static_cast<Underlying>(42),
           std::numeric_limits<Underlying>::min(),
           std::numeric_limits<Underlying>::max(),
       }) {
    T message{.e = valid_value};
    const char* error = nullptr;
    auto status = fidl_validate(coding_table, &message, sizeof(message), 0, &error);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error);
  }

  END_HELPER;
}

template <typename T>
bool TestInvalidEnum(const fidl_type_t* coding_table) {
  BEGIN_HELPER;

  // See extra_messages.test.fidl for the list of valid members.
  using Underlying = decltype(T::e);
  for (const Underlying invalid_value : {
           static_cast<Underlying>(7),
           static_cast<Underlying>(30),
           static_cast<Underlying>(std::numeric_limits<Underlying>::min() + 1),
           static_cast<Underlying>(std::numeric_limits<Underlying>::max() - 1),
       }) {
    T message{.e = invalid_value};
    const char* error = nullptr;
    auto status = fidl_validate(coding_table, &message, sizeof(message), 0, &error);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_STR_EQ(error, "not a valid enum member");
  }

  END_HELPER;
}

bool validate_int8_enum() {
  BEGIN_TEST;
  EXPECT_TRUE(TestValidEnum<Int8Enum>(&fidl_test_coding_Int8EnumStructTable));
  EXPECT_TRUE(TestInvalidEnum<Int8Enum>(&fidl_test_coding_Int8EnumStructTable));
  END_TEST;
}

bool validate_int16_enum() {
  BEGIN_TEST;
  EXPECT_TRUE(TestValidEnum<Int16Enum>(&fidl_test_coding_Int16EnumStructTable));
  EXPECT_TRUE(TestInvalidEnum<Int16Enum>(&fidl_test_coding_Int16EnumStructTable));
  END_TEST;
}

bool validate_int32_enum() {
  BEGIN_TEST;
  EXPECT_TRUE(TestValidEnum<Int32Enum>(&fidl_test_coding_Int32EnumStructTable));
  EXPECT_TRUE(TestInvalidEnum<Int32Enum>(&fidl_test_coding_Int32EnumStructTable));
  END_TEST;
}

bool validate_int64_enum() {
  BEGIN_TEST;
  EXPECT_TRUE(TestValidEnum<Int64Enum>(&fidl_test_coding_Int64EnumStructTable));
  EXPECT_TRUE(TestInvalidEnum<Int64Enum>(&fidl_test_coding_Int64EnumStructTable));
  END_TEST;
}

bool validate_uint8_enum() {
  BEGIN_TEST;
  EXPECT_TRUE(TestValidEnum<Uint8Enum>(&fidl_test_coding_Uint8EnumStructTable));
  EXPECT_TRUE(TestInvalidEnum<Uint8Enum>(&fidl_test_coding_Uint8EnumStructTable));
  END_TEST;
}

bool validate_uint16_enum() {
  BEGIN_TEST;
  EXPECT_TRUE(TestValidEnum<Uint16Enum>(&fidl_test_coding_Uint16EnumStructTable));
  EXPECT_TRUE(TestInvalidEnum<Uint16Enum>(&fidl_test_coding_Uint16EnumStructTable));
  END_TEST;
}

bool validate_uint32_enum() {
  BEGIN_TEST;
  EXPECT_TRUE(TestValidEnum<Uint32Enum>(&fidl_test_coding_Uint32EnumStructTable));
  EXPECT_TRUE(TestInvalidEnum<Uint32Enum>(&fidl_test_coding_Uint32EnumStructTable));
  END_TEST;
}

bool validate_uint64_enum() {
  BEGIN_TEST;
  EXPECT_TRUE(TestValidEnum<Uint64Enum>(&fidl_test_coding_Uint64EnumStructTable));
  EXPECT_TRUE(TestInvalidEnum<Uint64Enum>(&fidl_test_coding_Uint64EnumStructTable));
  END_TEST;
}

bool validate_primitives_struct() {
  BEGIN_TEST;

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
  static const fidl_type_t kBoolType = {.type_tag = kFidlTypePrimitive,
                                        .coded_primitive = kFidlCodedPrimitive_Bool};
  static const fidl_type_t kInt8Type = {.type_tag = kFidlTypePrimitive,
                                        .coded_primitive = kFidlCodedPrimitive_Int8};
  static const fidl_type_t kInt16Type = {.type_tag = kFidlTypePrimitive,
                                         .coded_primitive = kFidlCodedPrimitive_Int16};
  static const fidl_type_t kInt32Type = {.type_tag = kFidlTypePrimitive,
                                         .coded_primitive = kFidlCodedPrimitive_Int32};
  static const fidl_type_t kInt64Type = {.type_tag = kFidlTypePrimitive,
                                         .coded_primitive = kFidlCodedPrimitive_Int64};
  static const fidl_type_t kUint8Type = {.type_tag = kFidlTypePrimitive,
                                         .coded_primitive = kFidlCodedPrimitive_Uint8};
  static const fidl_type_t kUint16Type = {.type_tag = kFidlTypePrimitive,
                                          .coded_primitive = kFidlCodedPrimitive_Uint16};
  static const fidl_type_t kUint32Type = {.type_tag = kFidlTypePrimitive,
                                          .coded_primitive = kFidlCodedPrimitive_Uint32};
  static const fidl_type_t kUint64Type = {.type_tag = kFidlTypePrimitive,
                                          .coded_primitive = kFidlCodedPrimitive_Uint64};
  static const fidl_type_t kFloat32Type = {.type_tag = kFidlTypePrimitive,
                                           .coded_primitive = kFidlCodedPrimitive_Float32};
  static const fidl_type_t kFloat64Type = {.type_tag = kFidlTypePrimitive,
                                           .coded_primitive = kFidlCodedPrimitive_Float64};
  static const struct FidlStructField kFields[] = {
      {
          &kBoolType,
          0u,
          0u,
      },
      {
          &kInt8Type,
          1u,
          0u,
      },
      {
          &kInt16Type,
          2u,
          0u,
      },
      {
          &kInt32Type,
          4u,
          0u,
      },
      {
          &kInt64Type,
          8u,
          0u,
      },
      {
          &kUint8Type,
          16u,
          1u,
      },
      {
          &kUint16Type,
          18u,
          0u,
      },
      {
          &kUint32Type,
          20u,
          0u,
      },
      {
          &kUint64Type,
          24u,
          0u,
      },
      {
          &kFloat32Type,
          32u,
          4u,
      },
      {
          &kFloat64Type,
          40u,
          0u,
      },
  };
  static const fidl_type_t kPrimitiveStructCodingTable = {
      .type_tag = kFidlTypeStruct,
      {.coded_struct = {.fields = kFields,
                        .field_count = 11u,
                        .size = 48u,
                        .max_out_of_line = 0u,
                        .contains_union = false,
                        .name = "fidl.test.coding/PrimitiveStruct",
                        .alt_type = &kPrimitiveStructCodingTable}}};

  uint8_t data[kPrimitiveStructCodingTable.coded_struct.size];
  memset(data, 0, sizeof(data));

  const char* error = nullptr;
  auto status = fidl_validate(&kPrimitiveStructCodingTable, data,
                              static_cast<uint32_t>(sizeof(data)), 0, &error);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error);

  END_TEST;
}

BEGIN_TEST_CASE(null_parameters)
RUN_TEST(validate_null_validate_parameters)
END_TEST_CASE(null_parameters)

BEGIN_TEST_CASE(handles)
RUN_TEST(validate_single_present_handle)
RUN_TEST(validate_single_present_handle_check_trailing_padding)
RUN_TEST(validate_too_many_handles_specified_error)
RUN_TEST(validate_single_present_handle_unaligned_error)
RUN_TEST(validate_multiple_present_handles)
RUN_TEST(validate_single_absent_handle)
RUN_TEST(validate_multiple_absent_handles)
END_TEST_CASE(handles)

BEGIN_TEST_CASE(arrays)
RUN_TEST(validate_array_of_present_handles)
RUN_TEST(validate_array_of_nonnullable_handles_some_absent_error)
RUN_TEST(validate_array_of_nullable_handles)
RUN_TEST(validate_array_of_nullable_handles_with_insufficient_handles_error)
RUN_TEST(validate_array_of_array_of_present_handles)
RUN_TEST(validate_out_of_line_array)
END_TEST_CASE(arrays)

BEGIN_TEST_CASE(strings)
RUN_TEST(validate_present_nonnullable_string)
RUN_TEST(validate_multiple_present_nullable_string)
RUN_TEST(validate_present_nullable_string)
RUN_TEST(validate_absent_nonnullable_string_error)
RUN_TEST(validate_absent_nullable_string)
RUN_TEST(validate_present_nonnullable_bounded_string)
RUN_TEST(validate_present_nullable_bounded_string)
RUN_TEST(validate_absent_nonnullable_bounded_string_error)
RUN_TEST(validate_absent_nullable_bounded_string)
RUN_TEST(validate_present_nonnullable_bounded_string_short_error)
RUN_TEST(validate_present_nullable_bounded_string_short_error)
END_TEST_CASE(strings)

BEGIN_TEST_CASE(vectors)
RUN_TEST(validate_vector_with_huge_count)
RUN_TEST(validate_present_nonnullable_vector_of_handles)
RUN_TEST(validate_present_nullable_vector_of_handles)
RUN_TEST(validate_absent_nonnullable_vector_of_handles_error)
RUN_TEST(validate_absent_nullable_vector_of_handles)
RUN_TEST(validate_present_nonnullable_bounded_vector_of_handles)
RUN_TEST(validate_present_nullable_bounded_vector_of_handles)
RUN_TEST(validate_absent_nonnullable_bounded_vector_of_handles)
RUN_TEST(validate_absent_nullable_bounded_vector_of_handles)
RUN_TEST(validate_present_nonnullable_bounded_vector_of_handles_short_error)
RUN_TEST(validate_present_nullable_bounded_vector_of_handles_short_error)
RUN_TEST(validate_present_nonnullable_vector_of_uint32)
RUN_TEST(validate_present_nullable_vector_of_uint32)
RUN_TEST(validate_absent_nonnullable_vector_of_uint32_error)
RUN_TEST(validate_absent_nullable_vector_of_uint32)
RUN_TEST(validate_present_nonnullable_bounded_vector_of_uint32)
RUN_TEST(validate_present_nullable_bounded_vector_of_uint32)
RUN_TEST(validate_absent_nonnullable_bounded_vector_of_uint32)
RUN_TEST(validate_absent_nullable_bounded_vector_of_uint32)
RUN_TEST(validate_present_nonnullable_bounded_vector_of_uint32_short_error)
RUN_TEST(validate_present_nullable_bounded_vector_of_uint32_short_error)
END_TEST_CASE(vectors)

BEGIN_TEST_CASE(unions)
RUN_TEST(validate_bad_tagged_union_error)
RUN_TEST(validate_single_membered_present_nonnullable_union)
RUN_TEST(validate_many_membered_present_nonnullable_union)
RUN_TEST(validate_many_membered_present_nonnullable_union_check_padding)
RUN_TEST(validate_single_membered_present_nullable_union)
RUN_TEST(validate_many_membered_present_nullable_union)
RUN_TEST(validate_single_membered_absent_nullable_union)
RUN_TEST(validate_many_membered_absent_nullable_union)
END_TEST_CASE(unions)

BEGIN_TEST_CASE(structs)
RUN_TEST(validate_nested_nonnullable_structs)
RUN_TEST(validate_nested_nonnullable_structs_check_padding)
RUN_TEST(validate_nested_nullable_structs)
RUN_TEST(validate_nested_struct_recursion_too_deep_error)
END_TEST_CASE(structs)

BEGIN_TEST_CASE(xunions)
RUN_TEST(validate_valid_empty_nullable_xunion)
RUN_TEST(validate_empty_nonnullable_xunion)
RUN_TEST(validate_empty_nullable_xunion_nonzero_ordinal)
RUN_TEST(validate_nonempty_xunion_zero_ordinal)
RUN_TEST(validate_strict_xunion_unknown_ordinal)
RUN_TEST(validate_flexible_xunion_unknown_ordinal)
RUN_TEST(validate_nonempty_nullable_xunion_zero_ordinal)
END_TEST_CASE(xunions)

BEGIN_TEST_CASE(bits)
RUN_TEST(validate_zero_16bit_bits)
RUN_TEST(validate_valid_16bit_bits)
RUN_TEST(validate_invalid_16bit_bits)
RUN_TEST(validate_zero_32bit_bits)
RUN_TEST(validate_valid_32bit_bits)
RUN_TEST(validate_invalid_32bit_bits)
END_TEST_CASE(bits)

BEGIN_TEST_CASE(enums)
RUN_TEST(validate_int8_enum)
RUN_TEST(validate_int16_enum)
RUN_TEST(validate_int32_enum)
RUN_TEST(validate_int64_enum)
RUN_TEST(validate_uint8_enum)
RUN_TEST(validate_uint16_enum)
RUN_TEST(validate_uint32_enum)
RUN_TEST(validate_uint64_enum)
END_TEST_CASE(enums)

BEGIN_TEST_CASE(primitives)
RUN_TEST(validate_primitives_struct)
END_TEST_CASE(primitives)

}  // namespace
}  // namespace fidl
