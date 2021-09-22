// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>
#include <lib/fidl/llcpp/coding.h>
#include <limits.h>
#include <stddef.h>

#ifdef __Fuchsia__
#include <lib/zx/event.h>
#include <lib/zx/eventpair.h>
#include <zircon/syscalls.h>
#endif

#include <memory>
#include <vector>

#include <zxtest/zxtest.h>

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

#ifdef __Fuchsia__
// Check if the other end of the eventpair is valid
bool IsPeerValid(const zx::unowned_eventpair handle) {
  zx_signals_t observed_signals = {};
  switch (handle->wait_one(ZX_EVENTPAIR_PEER_CLOSED, zx::deadline_after(zx::msec(1)),
                           &observed_signals)) {
    case ZX_ERR_TIMED_OUT:
      // timeout implies peer-closed was not observed
      return true;
    case ZX_OK:
      return (observed_signals & ZX_EVENTPAIR_PEER_CLOSED) == 0;
    default:
      return false;
  }
}
#endif

enum class Mode { EncodeOnly, Iovec1FullyLinearized, Iovec16 };

template <Mode mode>
zx_status_t encode_helper(const fidl_type_t* type, void* value, uint8_t* out_bytes,
                          uint32_t num_bytes, zx_handle_t* out_handles, uint32_t num_handles,
                          uint32_t* out_num_actual_bytes, uint32_t* out_num_actual_handles,
                          const char** out_error_msg) {
  __builtin_unreachable();  // non-specialized mode should never happen
}

template <>
zx_status_t encode_helper<Mode::EncodeOnly>(const fidl_type_t* type, void* value,
                                            uint8_t* out_bytes, uint32_t num_bytes,
                                            zx_handle_t* out_handles, uint32_t num_handles,
                                            uint32_t* out_num_actual_bytes,
                                            uint32_t* out_num_actual_handles,
                                            const char** out_error_msg) {
  zx_status_t status = fidl_encode(type, value, num_bytes, out_handles, num_handles,
                                   out_num_actual_handles, out_error_msg);
  if (out_bytes && value) {
    memcpy(out_bytes, value, num_bytes);
  }
  return status;
}

zx_status_t iovec_encode_helper_impl(uint32_t num_iovec, const fidl_type_t* type, void* value,
                                     uint8_t* bytes, uint32_t num_bytes, zx_handle_t* handles,
                                     uint32_t num_handles, uint32_t* out_actual_bytes,
                                     uint32_t* out_actual_handles, const char** out_error_msg) {
  std::unique_ptr<zx_handle_disposition_t[]> handle_dispositions;
  if (handles != nullptr) {
    handle_dispositions = std::make_unique<zx_handle_disposition_t[]>(num_handles);
  }
  auto iovecs = std::make_unique<zx_channel_iovec_t[]>(num_iovec);
  auto backing_buffer = std::make_unique<uint8_t[]>(num_bytes);
  uint32_t actual_iovecs = 0;
  zx_status_t status = fidl::internal::EncodeIovecEtc<FIDL_WIRE_FORMAT_VERSION_V2>(
      type, value, iovecs.get(), num_iovec, handle_dispositions.get(), num_handles,
      backing_buffer.get(), num_bytes, &actual_iovecs, out_actual_handles, out_error_msg);
  if (status != ZX_OK) {
    return status;
  }
  if (out_actual_bytes == nullptr) {
    *out_error_msg = "Cannot encode with null out_actual_bytes";
    FidlHandleDispositionCloseMany(handle_dispositions.get(), *out_actual_handles);
    return ZX_ERR_INVALID_ARGS;
  }
  *out_actual_bytes = 0;
  for (uint32_t i = 0; i < actual_iovecs; i++) {
    zx_channel_iovec_t& iovec = iovecs[i];
    if (*out_actual_bytes + iovec.capacity > num_bytes) {
      *out_error_msg = "pointed offset exceeds buffer size";
      FidlHandleDispositionCloseMany(handle_dispositions.get(), *out_actual_handles);
      return ZX_ERR_INVALID_ARGS;
    }
    memcpy(bytes + *out_actual_bytes, iovec.buffer, iovec.capacity);
    *out_actual_bytes += iovec.capacity;
  }
  if (handles != nullptr) {
    for (uint32_t i = 0; i < *out_actual_handles; i++) {
      handles[i] = handle_dispositions[i].handle;
    }
  } else {
    // If no out arg, close any handles that were created.
    FidlHandleDispositionCloseMany(handle_dispositions.get(), *out_actual_handles);
  }
  return ZX_OK;
}

template <>
zx_status_t encode_helper<Mode::Iovec1FullyLinearized>(
    const fidl_type_t* type, void* value, uint8_t* out_bytes, uint32_t num_bytes,
    zx_handle_t* out_handles, uint32_t num_handles, uint32_t* out_num_actual_bytes,
    uint32_t* out_num_actual_handles, const char** out_error_msg) {
  return iovec_encode_helper_impl(1u, type, value, out_bytes, num_bytes, out_handles, num_handles,
                                  out_num_actual_bytes, out_num_actual_handles, out_error_msg);
}

template <>
zx_status_t encode_helper<Mode::Iovec16>(const fidl_type_t* type, void* value, uint8_t* out_bytes,
                                         uint32_t num_bytes, zx_handle_t* out_handles,
                                         uint32_t num_handles, uint32_t* out_num_actual_bytes,
                                         uint32_t* out_num_actual_handles,
                                         const char** out_error_msg) {
  return iovec_encode_helper_impl(16u, type, value, out_bytes, num_bytes, out_handles, num_handles,
                                  out_num_actual_bytes, out_num_actual_handles, out_error_msg);
}

template <Mode mode>
void encode_null_encode_parameters() {
// Null message type.
#ifdef __Fuchsia__
  if (mode == Mode::EncodeOnly) {
    nonnullable_handle_message_layout message;
    uint8_t buf[sizeof(nonnullable_handle_message_layout)];
    zx_handle_t handles[1] = {};
    const char* error = nullptr;
    uint32_t actual_bytes = 0u;
    uint32_t actual_handles = 0u;
    auto status = encode_helper<mode>(nullptr, &message, buf, ArrayCount(buf), handles,
                                      ArrayCount(handles), &actual_bytes, &actual_handles, &error);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NOT_NULL(error);
  }

  // Null message.
  {
    uint8_t buf[sizeof(nonnullable_handle_message_layout)];
    zx_handle_t handles[1] = {};
    const char* error = nullptr;
    uint32_t actual_bytes = 0u;
    uint32_t actual_handles = 0u;
    auto status =
        encode_helper<mode>(&nonnullable_handle_message_type, nullptr, buf, ArrayCount(buf),
                            handles, ArrayCount(handles), &actual_bytes, &actual_handles, &error);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NOT_NULL(error);
  }
#endif

  // Null handles, for a message that has a handle.
  {
    nonnullable_handle_message_layout message;
    uint8_t buf[sizeof(nonnullable_handle_message_layout)];
    const char* error = nullptr;
    uint32_t actual_bytes = 0u;
    uint32_t actual_handles = 0u;
    auto status =
        encode_helper<mode>(&nonnullable_handle_message_type, &message, buf, ArrayCount(buf),
                            nullptr, 0, &actual_bytes, &actual_handles, &error);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NOT_NULL(error);
  }

  // Null handles but positive handle count.
  {
    nonnullable_handle_message_layout message;
    uint8_t buf[sizeof(nonnullable_handle_message_layout)];
    const char* error = nullptr;
    uint32_t actual_bytes = 0u;
    uint32_t actual_handles = 0u;
    auto status =
        encode_helper<mode>(&nonnullable_handle_message_type, &message, buf, ArrayCount(buf),
                            nullptr, 1, &actual_bytes, &actual_handles, &error);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NOT_NULL(error);
  }

// A null actual byte count pointer.
#ifdef __Fuchsia__
  // A null actual handle count pointer.
  if (mode == Mode::EncodeOnly) {
    nonnullable_handle_message_layout message;
    uint8_t buf[sizeof(nonnullable_handle_message_layout)];
    zx_handle_t handles[1] = {};
    const char* error = nullptr;
    uint32_t actual_bytes = 0u;
    auto status =
        encode_helper<mode>(&nonnullable_handle_message_type, &message, buf, ArrayCount(buf),
                            handles, ArrayCount(handles), &actual_bytes, nullptr, &error);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NOT_NULL(error);
  }
#endif

  // A null error string pointer is ok, though.
  if (mode == Mode::EncodeOnly) {
    uint32_t actual_bytes = 0u;
    uint32_t actual_handles = 0u;
    auto status = encode_helper<mode>(nullptr, nullptr, nullptr, 0u, nullptr, 0u, &actual_bytes,
                                      &actual_handles, nullptr);
    EXPECT_NE(status, ZX_OK);
  }

// A null error is also ok in success cases.
#ifdef __Fuchsia__
  if (mode == Mode::EncodeOnly) {
    nonnullable_handle_message_layout message = {};
    message.inline_struct.handle = dummy_handle_0;
    uint8_t buf[sizeof(nonnullable_handle_message_layout)];
    zx_handle_t handles[1] = {};

    uint32_t actual_bytes = 0u;
    uint32_t actual_handles = 0u;
    auto status =
        encode_helper<mode>(&nonnullable_handle_message_type, &message, buf, ArrayCount(buf),
                            handles, ArrayCount(handles), &actual_bytes, &actual_handles, nullptr);
    auto& result = *reinterpret_cast<nonnullable_handle_message_layout*>(buf);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(actual_handles, 1u);
    EXPECT_EQ(handles[0], dummy_handle_0);
    EXPECT_EQ(result.inline_struct.handle, FIDL_HANDLE_PRESENT);
  }
#endif
}

TEST(NullParameters, encode_null_encode_parameters_Mode_EncodeOnly) {
  encode_null_encode_parameters<Mode::EncodeOnly>();
}
TEST(NullParameters, encode_null_encode_parameters_Mode_Iovec1FullyLinearized) {
  encode_null_encode_parameters<Mode::Iovec1FullyLinearized>();
}
TEST(NullParameters, encode_null_encode_parameters_Mode_Iovec16) {
  encode_null_encode_parameters<Mode::Iovec16>();
}

#ifdef __Fuchsia__
TEST(BufferSizes, encode_too_many_bytes_specified_should_close_handles) {
  zx::eventpair ep0, ep1;
  ASSERT_EQ(zx::eventpair::create(0, &ep0, &ep1), ZX_OK);

  constexpr size_t kSizeTooBig = sizeof(nonnullable_handle_message_layout) * 2;
  uint8_t buffer[kSizeTooBig];
  nonnullable_handle_message_layout& message =
      *reinterpret_cast<nonnullable_handle_message_layout*>(buffer);
  message.inline_struct.handle = ep0.get();

  ASSERT_TRUE(IsPeerValid(zx::unowned_eventpair(ep1)));

  zx_handle_t handles[1] = {};
  const char* error = nullptr;
  uint32_t actual_handles = 1234;
  auto status = fidl_encode(&nonnullable_handle_message_type, &message, kSizeTooBig, handles,
                            ArrayCount(handles), &actual_handles, &error);

  ASSERT_EQ(status, ZX_ERR_INVALID_ARGS);
  ASSERT_NOT_NULL(error);
  ASSERT_EQ(actual_handles, 0);
  ASSERT_EQ(message.inline_struct.handle, FIDL_HANDLE_PRESENT);
  ASSERT_EQ(handles[0], ep0.get());
  ASSERT_FALSE(IsPeerValid(zx::unowned_eventpair(ep1)));

  // When the test succeeds, |ep0| is closed by the encoder.
  zx_handle_t unused = ep0.release();
  (void)unused;
}

template <Mode mode>
void encode_single_present_handle_unaligned_error() {
  // Test a short, unaligned version of nonnullable message
  // handle. All fidl message objects should be 8 byte aligned.
  //
  // We use a byte array rather than fidl_message_header_t to avoid
  // aligning to 8 bytes.
  struct unaligned_nonnullable_handle_inline_data {
    uint8_t header[sizeof(fidl_message_header_t)];
    zx_handle_t handle;
  };
  struct unaligned_nonnullable_handle_message_layout {
    unaligned_nonnullable_handle_inline_data inline_struct;
  };

  unaligned_nonnullable_handle_message_layout message = {};
  message.inline_struct.handle = dummy_handle_0;
  uint8_t buf[sizeof(unaligned_nonnullable_handle_message_layout)];

  zx_handle_t handles[1] = {};

  // Encoding the unaligned version of the struct should fail.
  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&nonnullable_handle_message_type, &message, buf, ArrayCount(buf), handles,
                          ArrayCount(handles), &actual_bytes, &actual_handles, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
}
#endif

#if __Fuchsia__
TEST(Unaligned, encode_single_present_handle_unaligned_error_Mode_EncodeOnly) {
  encode_single_present_handle_unaligned_error<Mode::EncodeOnly>();
}
TEST(Unaligned, encode_single_present_handle_unaligned_error_Mode_Iovec1FullyLinearized) {
  encode_single_present_handle_unaligned_error<Mode::Iovec1FullyLinearized>();
}
TEST(Unaligned, encode_single_present_handle_unaligned_error_Mode_Iovec16) {
  encode_single_present_handle_unaligned_error<Mode::Iovec16>();
}
#endif

template <Mode mode>
void encode_present_nonnullable_string_unaligned_error() {
  unbounded_nonnullable_string_message_layout message = {};
  message.inline_struct.string = fidl_string_t{6, &message.data[0]};
  memcpy(message.data, "hello!", 6);

  // Copy the message to unaligned storage one byte off from true alignment
  uint8_t message_storage[sizeof(unbounded_nonnullable_string_message_layout) + 1];
  auto* unaligned_ptr = &message_storage[0] + 1;
  memcpy(unaligned_ptr, &message, sizeof(message));

  // Pointer patch the copied message
  // NOTE: this code must be kept in sync with the layout in fidl_structs.h.
  // The offset is calculated manually because casting to the layout type and
  // accessing its members leads to an unaligned access error with UBSan
  // (see fxbug.dev/55300)
  auto* string_data_ptr = unaligned_ptr +
                          offsetof(unbounded_nonnullable_string_inline_data, string) +
                          offsetof(fidl_string_t, data);
  auto patched_ptr_val = reinterpret_cast<uintptr_t>(
      unaligned_ptr + offsetof(unbounded_nonnullable_string_message_layout, data));
  memcpy(string_data_ptr, &patched_ptr_val, sizeof(patched_ptr_val));

  uint8_t buf[sizeof(unbounded_nonnullable_string_message_layout)];

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto unaligned_msg =
      reinterpret_cast<unbounded_nonnullable_string_message_layout*>(unaligned_ptr);
  auto status =
      encode_helper<mode>(&unbounded_nonnullable_string_message_type, unaligned_msg, buf,
                          ArrayCount(buf), nullptr, 0, &actual_bytes, &actual_handles, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
  ASSERT_SUBSTR(error, "must be aligned to FIDL_ALIGNMENT");
}

TEST(Unaligned, encode_present_nonnullable_string_unaligned_error_Mode_EncodeOnly) {
  encode_present_nonnullable_string_unaligned_error<Mode::EncodeOnly>();
}
TEST(Unaligned, encode_present_nonnullable_string_unaligned_error_Mode_Iovec1FullyLinearized) {
  encode_present_nonnullable_string_unaligned_error<Mode::Iovec1FullyLinearized>();
}
TEST(Unaligned, encode_present_nonnullable_string_unaligned_error_Mode_Iovec16) {
  encode_present_nonnullable_string_unaligned_error<Mode::Iovec16>();
}

#ifdef __Fuchsia__
template <Mode mode>
void encode_single_present_handle() {
  nonnullable_handle_message_layout message = {};
  message.inline_struct.handle = dummy_handle_0;

  uint8_t buf[sizeof(nonnullable_handle_message_layout)];
  zx_handle_t handles[1] = {};

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&nonnullable_handle_message_type, &message, buf, ArrayCount(buf), handles,
                          ArrayCount(handles), &actual_bytes, &actual_handles, &error);
  auto& result = *reinterpret_cast<nonnullable_handle_message_layout*>(buf);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(actual_handles, 1u);
  EXPECT_EQ(handles[0], dummy_handle_0);
  if (mode == Mode::Iovec1FullyLinearized || mode == Mode::Iovec16) {
    EXPECT_EQ(message.inline_struct.handle, ZX_HANDLE_INVALID);
  }
  EXPECT_EQ(result.inline_struct.handle, FIDL_HANDLE_PRESENT);
}

template <Mode mode>
void encode_single_present_handle_zero_trailing_padding() {
  // Initialize a buffer with garbage value of 0xAA.
  constexpr size_t kBufferSize = sizeof(nonnullable_handle_message_layout);
  uint8_t buffer[kBufferSize];
  memset(buffer, 0xAA, sizeof(buffer));

  nonnullable_handle_message_layout* message = new (&buffer[0]) nonnullable_handle_message_layout;
  message->inline_struct.handle = dummy_handle_0;

  EXPECT_EQ(buffer[kBufferSize - 4], 0xAA);
  EXPECT_EQ(buffer[kBufferSize - 3], 0xAA);
  EXPECT_EQ(buffer[kBufferSize - 2], 0xAA);
  EXPECT_EQ(buffer[kBufferSize - 1], 0xAA);

  uint8_t out_buffer[kBufferSize];
  zx_handle_t handles[1] = {};

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status = encode_helper<mode>(&nonnullable_handle_message_type, message, out_buffer,
                                    ArrayCount(out_buffer), handles, ArrayCount(handles),
                                    &actual_bytes, &actual_handles, &error);
  auto& result = *reinterpret_cast<nonnullable_handle_message_layout*>(out_buffer);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(actual_handles, 1u);
  EXPECT_EQ(handles[0], dummy_handle_0);
  if (mode == Mode::Iovec1FullyLinearized || mode == Mode::Iovec16) {
    EXPECT_EQ(message->inline_struct.handle, ZX_HANDLE_INVALID);
  }
  EXPECT_EQ(result.inline_struct.handle, FIDL_HANDLE_PRESENT);

  // Last 4 bytes are trailing padding after the handle and before the end of the structure.
  // Despite being initialized to 0xAA, these should be set to zero by the encoder.
  EXPECT_EQ(out_buffer[kBufferSize - 4], 0);
  EXPECT_EQ(out_buffer[kBufferSize - 3], 0);
  EXPECT_EQ(out_buffer[kBufferSize - 2], 0);
  EXPECT_EQ(out_buffer[kBufferSize - 1], 0);
}

template <Mode mode>
void encode_multiple_present_handles() {
  multiple_nonnullable_handles_message_layout message = {};
  message.inline_struct.handle_0 = dummy_handle_0;
  message.inline_struct.handle_1 = dummy_handle_1;
  message.inline_struct.handle_2 = dummy_handle_2;

  uint8_t buf[sizeof(multiple_nonnullable_handles_message_layout)];

  zx_handle_t handles[3] = {};

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status = encode_helper<mode>(&multiple_nonnullable_handles_message_type, &message, buf,
                                    ArrayCount(buf), handles, ArrayCount(handles), &actual_bytes,
                                    &actual_handles, &error);
  auto& result = *reinterpret_cast<multiple_nonnullable_handles_message_layout*>(buf);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(actual_handles, 3u);
  EXPECT_EQ(result.inline_struct.data_0, 0u);
  EXPECT_EQ(result.inline_struct.handle_0, FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.inline_struct.data_1, 0u);
  EXPECT_EQ(result.inline_struct.handle_1, FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.inline_struct.handle_2, FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.inline_struct.data_2, 0u);
  if (mode == Mode::Iovec1FullyLinearized || mode == Mode::Iovec16) {
    EXPECT_EQ(message.inline_struct.handle_0, ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.handle_1, ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.handle_2, ZX_HANDLE_INVALID);
  }
  EXPECT_EQ(handles[0], dummy_handle_0);
  EXPECT_EQ(handles[1], dummy_handle_1);
  EXPECT_EQ(handles[2], dummy_handle_2);
}

template <Mode mode>
void encode_single_absent_handle() {
  nullable_handle_message_layout message = {};
  message.inline_struct.handle = ZX_HANDLE_INVALID;

  uint8_t buf[sizeof(nullable_handle_message_layout)];

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status = encode_helper<mode>(&nullable_handle_message_type, &message, buf, ArrayCount(buf),
                                    nullptr, 0, &actual_bytes, &actual_handles, &error);
  auto& result = *reinterpret_cast<nullable_handle_message_layout*>(buf);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(actual_handles, 0u);
  EXPECT_EQ(result.inline_struct.handle, FIDL_HANDLE_ABSENT);
  if (mode == Mode::Iovec1FullyLinearized || mode == Mode::Iovec16) {
    EXPECT_EQ(message.inline_struct.handle, ZX_HANDLE_INVALID);
  }
}

template <Mode mode>
void encode_multiple_absent_handles() {
  multiple_nullable_handles_message_layout message = {};
  message.inline_struct.handle_0 = ZX_HANDLE_INVALID;
  message.inline_struct.handle_1 = ZX_HANDLE_INVALID;
  message.inline_struct.handle_2 = ZX_HANDLE_INVALID;
  uint8_t buf[sizeof(multiple_nullable_handles_message_layout)];

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&multiple_nullable_handles_message_type, &message, buf, ArrayCount(buf),
                          nullptr, 0, &actual_bytes, &actual_handles, &error);
  auto& result = *reinterpret_cast<multiple_nullable_handles_message_layout*>(buf);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(actual_handles, 0u);
  EXPECT_EQ(result.inline_struct.data_0, 0u);
  EXPECT_EQ(result.inline_struct.handle_0, FIDL_HANDLE_ABSENT);
  EXPECT_EQ(result.inline_struct.data_1, 0u);
  EXPECT_EQ(result.inline_struct.handle_1, FIDL_HANDLE_ABSENT);
  EXPECT_EQ(result.inline_struct.handle_2, FIDL_HANDLE_ABSENT);
  EXPECT_EQ(result.inline_struct.data_2, 0u);
  if (mode == Mode::Iovec1FullyLinearized || mode == Mode::Iovec16) {
    EXPECT_EQ(message.inline_struct.handle_0, ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.handle_1, ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.handle_2, ZX_HANDLE_INVALID);
  }
}

template <Mode mode>
void encode_array_of_present_handles() {
  array_of_nonnullable_handles_message_layout message = {};
  message.inline_struct.handles[0] = dummy_handle_0;
  message.inline_struct.handles[1] = dummy_handle_1;
  message.inline_struct.handles[2] = dummy_handle_2;
  message.inline_struct.handles[3] = dummy_handle_3;
  uint8_t buf[sizeof(array_of_nonnullable_handles_message_layout)];

  zx_handle_t handles[4] = {};

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status = encode_helper<mode>(&array_of_nonnullable_handles_message_type, &message, buf,
                                    ArrayCount(buf), handles, ArrayCount(handles), &actual_bytes,
                                    &actual_handles, &error);
  auto& result = *reinterpret_cast<array_of_nonnullable_handles_message_layout*>(buf);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(actual_handles, 4u);
  if (mode == Mode::Iovec1FullyLinearized || mode == Mode::Iovec16) {
    EXPECT_EQ(message.inline_struct.handles[0], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.handles[1], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.handles[2], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.handles[3], ZX_HANDLE_INVALID);
  }
  EXPECT_EQ(result.inline_struct.handles[0], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.inline_struct.handles[1], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.inline_struct.handles[2], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.inline_struct.handles[3], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(handles[0], dummy_handle_0);
  EXPECT_EQ(handles[1], dummy_handle_1);
  EXPECT_EQ(handles[2], dummy_handle_2);
  EXPECT_EQ(handles[3], dummy_handle_3);
}

#ifdef __Fuchsia__
template <Mode mode>
void encode_array_of_present_handles_error_closes_handles() {
  array_of_nonnullable_handles_message_layout message = {};
  zx_handle_t handle_pairs[4][2];
  // Use eventpairs so that we can know for sure that handles were closed by
  // EncodeIovecEtc<FIDL_WIRE_FORMAT_VERSION_V2>.
  for (uint32_t i = 0; i < ArrayCount(handle_pairs); ++i) {
    ASSERT_EQ(zx_eventpair_create(0u, &handle_pairs[i][0], &handle_pairs[i][1]), ZX_OK);
  }
  message.inline_struct.handles[0] = handle_pairs[0][0];
  message.inline_struct.handles[1] = handle_pairs[1][0];
  message.inline_struct.handles[2] = handle_pairs[2][0];
  message.inline_struct.handles[3] = handle_pairs[3][0];

  uint8_t buf[sizeof(array_of_nonnullable_handles_message_layout)];

  zx_handle_t output_handles[4] = {};

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status = encode_helper<mode>(
      &array_of_nonnullable_handles_message_type, &message, buf, ArrayCount(buf), output_handles,
      // -2 makes this invalid.
      ArrayCount(message.inline_struct.handles) - 2, &actual_bytes, &actual_handles, &error);
  // Should fail because we we pass in a max_handles < the actual number of handles.
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(actual_handles, 0);
  // All handles should be closed, those before the error was encountered and those after.
  for (uint32_t i = 0; i < ArrayCount(handle_pairs); ++i) {
    zx_signals_t observed_signals;
    EXPECT_EQ(zx_object_wait_one(handle_pairs[i][1], ZX_EVENTPAIR_PEER_CLOSED,
                                 1,  // deadline shouldn't matter, should return immediately.
                                 &observed_signals),
              ZX_OK);
    EXPECT_EQ(observed_signals & ZX_EVENTPAIR_PEER_CLOSED, ZX_EVENTPAIR_PEER_CLOSED);
    EXPECT_EQ(zx_handle_close(handle_pairs[i][1]),
              ZX_OK);  // [i][0] was closed by EncodeIovecEtc<FIDL_WIRE_FORMAT_VERSION_V2>.
  }
}
#endif

template <Mode mode>
void encode_array_of_nullable_handles() {
  array_of_nullable_handles_message_layout message = {};
  message.inline_struct.handles[0] = dummy_handle_0;
  message.inline_struct.handles[1] = ZX_HANDLE_INVALID;
  message.inline_struct.handles[2] = dummy_handle_1;
  message.inline_struct.handles[3] = ZX_HANDLE_INVALID;
  message.inline_struct.handles[4] = dummy_handle_2;

  uint8_t buf[sizeof(array_of_nullable_handles_message_layout)];

  zx_handle_t handles[3] = {};

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&array_of_nullable_handles_message_type, &message, buf, ArrayCount(buf),
                          handles, ArrayCount(handles), &actual_bytes, &actual_handles, &error);
  auto& result = *reinterpret_cast<array_of_nullable_handles_message_layout*>(buf);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(actual_handles, 3u);
  if (mode == Mode::Iovec1FullyLinearized || mode == Mode::Iovec16) {
    EXPECT_EQ(message.inline_struct.handles[0], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.handles[1], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.handles[2], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.handles[3], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.handles[4], ZX_HANDLE_INVALID);
  }
  EXPECT_EQ(result.inline_struct.handles[0], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.inline_struct.handles[1], FIDL_HANDLE_ABSENT);
  EXPECT_EQ(result.inline_struct.handles[2], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.inline_struct.handles[3], FIDL_HANDLE_ABSENT);
  EXPECT_EQ(result.inline_struct.handles[4], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(handles[0], dummy_handle_0);
  EXPECT_EQ(handles[1], dummy_handle_1);
  EXPECT_EQ(handles[2], dummy_handle_2);
}

template <Mode mode>
void encode_array_of_nullable_handles_with_insufficient_handles_error() {
  array_of_nullable_handles_message_layout message = {};
  message.inline_struct.handles[0] = dummy_handle_0;
  message.inline_struct.handles[1] = ZX_HANDLE_INVALID;
  message.inline_struct.handles[2] = dummy_handle_1;
  message.inline_struct.handles[3] = ZX_HANDLE_INVALID;
  message.inline_struct.handles[4] = dummy_handle_2;

  uint8_t buf[sizeof(array_of_nullable_handles_message_layout)];

  zx_handle_t handles[2] = {};

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&array_of_nullable_handles_message_type, &message, buf, ArrayCount(buf),
                          handles, ArrayCount(handles), &actual_bytes, &actual_handles, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
}

template <Mode mode>
void encode_array_of_array_of_present_handles() {
  array_of_array_of_nonnullable_handles_message_layout message = {};
  message.inline_struct.handles[0][0] = dummy_handle_0;
  message.inline_struct.handles[0][1] = dummy_handle_1;
  message.inline_struct.handles[0][2] = dummy_handle_2;
  message.inline_struct.handles[0][3] = dummy_handle_3;
  message.inline_struct.handles[1][0] = dummy_handle_4;
  message.inline_struct.handles[1][1] = dummy_handle_5;
  message.inline_struct.handles[1][2] = dummy_handle_6;
  message.inline_struct.handles[1][3] = dummy_handle_7;
  message.inline_struct.handles[2][0] = dummy_handle_8;
  message.inline_struct.handles[2][1] = dummy_handle_9;
  message.inline_struct.handles[2][2] = dummy_handle_10;
  message.inline_struct.handles[2][3] = dummy_handle_11;

  uint8_t buf[sizeof(array_of_array_of_nonnullable_handles_message_layout)];

  zx_handle_t handles[12] = {};

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status = encode_helper<mode>(&array_of_array_of_nonnullable_handles_message_type, &message,
                                    buf, ArrayCount(buf), handles, ArrayCount(handles),
                                    &actual_bytes, &actual_handles, &error);
  auto& result = *reinterpret_cast<array_of_array_of_nonnullable_handles_message_layout*>(buf);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(actual_handles, 12u);
  if (mode == Mode::Iovec1FullyLinearized || mode == Mode::Iovec16) {
    EXPECT_EQ(message.inline_struct.handles[0][0], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.handles[0][1], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.handles[0][2], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.handles[0][3], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.handles[1][0], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.handles[1][1], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.handles[1][2], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.handles[1][3], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.handles[2][0], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.handles[2][1], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.handles[2][2], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.handles[2][3], ZX_HANDLE_INVALID);
  }
  EXPECT_EQ(result.inline_struct.handles[0][0], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.inline_struct.handles[0][1], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.inline_struct.handles[0][2], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.inline_struct.handles[0][3], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.inline_struct.handles[1][0], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.inline_struct.handles[1][1], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.inline_struct.handles[1][2], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.inline_struct.handles[1][3], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.inline_struct.handles[2][0], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.inline_struct.handles[2][1], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.inline_struct.handles[2][2], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.inline_struct.handles[2][3], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(handles[0], dummy_handle_0);
  EXPECT_EQ(handles[1], dummy_handle_1);
  EXPECT_EQ(handles[2], dummy_handle_2);
  EXPECT_EQ(handles[3], dummy_handle_3);
  EXPECT_EQ(handles[4], dummy_handle_4);
  EXPECT_EQ(handles[5], dummy_handle_5);
  EXPECT_EQ(handles[6], dummy_handle_6);
  EXPECT_EQ(handles[7], dummy_handle_7);
  EXPECT_EQ(handles[8], dummy_handle_8);
  EXPECT_EQ(handles[9], dummy_handle_9);
  EXPECT_EQ(handles[10], dummy_handle_10);
  EXPECT_EQ(handles[11], dummy_handle_11);
}

template <Mode mode>
void encode_out_of_line_array_of_nonnullable_handles() {
  out_of_line_array_of_nonnullable_handles_message_layout message = {};
  message.inline_struct.maybe_array = &message.data;
  message.data.handles[0] = dummy_handle_0;
  message.data.handles[1] = dummy_handle_1;
  message.data.handles[2] = dummy_handle_2;
  message.data.handles[3] = dummy_handle_3;

  uint8_t buf[sizeof(out_of_line_array_of_nonnullable_handles_message_layout)];
  zx_handle_t handles[4] = {};

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status = encode_helper<mode>(&out_of_line_array_of_nonnullable_handles_message_type,
                                    &message, buf, ArrayCount(buf), handles, ArrayCount(handles),
                                    &actual_bytes, &actual_handles, &error);
  auto& result = *reinterpret_cast<out_of_line_array_of_nonnullable_handles_message_layout*>(buf);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(actual_handles, 4u);

  auto array_ptr = reinterpret_cast<uint64_t>(result.inline_struct.maybe_array);
  EXPECT_EQ(array_ptr, FIDL_ALLOC_PRESENT);
  if (mode == Mode::Iovec1FullyLinearized || mode == Mode::Iovec16) {
    EXPECT_EQ(message.data.handles[0], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.data.handles[1], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.data.handles[2], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.data.handles[3], ZX_HANDLE_INVALID);
  }
  EXPECT_EQ(result.data.handles[0], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.data.handles[1], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.data.handles[2], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.data.handles[3], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(handles[0], dummy_handle_0);
  EXPECT_EQ(handles[1], dummy_handle_1);
  EXPECT_EQ(handles[2], dummy_handle_2);
  EXPECT_EQ(handles[3], dummy_handle_3);
}
#endif

template <Mode mode>
void encode_present_nonnullable_string() {
  unbounded_nonnullable_string_message_layout message = {};
  message.inline_struct.string = fidl_string_t{6, &message.data[0]};
  memcpy(message.data, "hello!", 6);

  uint8_t buf[sizeof(unbounded_nonnullable_string_message_layout)];

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&unbounded_nonnullable_string_message_type, &message, buf,
                          ArrayCount(buf), nullptr, 0, &actual_bytes, &actual_handles, &error);

  auto& result = *reinterpret_cast<unbounded_nonnullable_string_message_layout*>(buf);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(actual_handles, 0u);
  EXPECT_EQ(reinterpret_cast<uint64_t>(result.inline_struct.string.data), FIDL_ALLOC_PRESENT);
  EXPECT_EQ(result.inline_struct.string.size, 6);
  EXPECT_EQ(result.data[0], 'h');
  EXPECT_EQ(result.data[1], 'e');
  EXPECT_EQ(result.data[2], 'l');
  EXPECT_EQ(result.data[3], 'l');
  EXPECT_EQ(result.data[4], 'o');
  EXPECT_EQ(result.data[5], '!');
}

template <Mode mode>
void encode_present_nullable_string() {
  unbounded_nullable_string_message_layout message = {};
  message.inline_struct.string = fidl_string_t{6, &message.data[0]};
  memcpy(message.data, "hello!", 6);

  uint8_t buf[sizeof(unbounded_nullable_string_message_layout)];

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&unbounded_nullable_string_message_type, &message, buf, ArrayCount(buf),
                          nullptr, 0, &actual_bytes, &actual_handles, &error);
  auto& result = *reinterpret_cast<unbounded_nullable_string_message_layout*>(buf);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(actual_handles, 0u);
  EXPECT_EQ(result.inline_struct.string.size, 6);
  EXPECT_EQ(result.data[0], 'h');
  EXPECT_EQ(result.data[1], 'e');
  EXPECT_EQ(result.data[2], 'l');
  EXPECT_EQ(result.data[3], 'l');
  EXPECT_EQ(result.data[4], 'o');
  EXPECT_EQ(result.data[5], '!');
}

template <Mode mode>
void encode_multiple_present_nullable_string() {
  // Among other things, this test ensures we handle out-of-line
  // alignment to FIDL_ALIGNMENT (i.e., 8) bytes correctly.
  multiple_nullable_strings_message_layout message;
  message.inline_struct.string = fidl_string_t{6, &message.data[0]};
  message.inline_struct.string2 = fidl_string_t{8, &message.data2[0]};
  memcpy(message.data, "hello ", 6);
  memcpy(message.data2, "world!!!", 8);

  uint8_t buf[sizeof(multiple_nullable_strings_message_layout)];

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&multiple_nullable_strings_message_type, &message, buf, ArrayCount(buf),
                          nullptr, 0, &actual_bytes, &actual_handles, &error);

  auto& result = *reinterpret_cast<multiple_nullable_strings_message_layout*>(buf);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(actual_handles, 0u);
  EXPECT_EQ(result.inline_struct.string.size, 6);
  EXPECT_EQ(reinterpret_cast<uint64_t>(result.inline_struct.string.data), FIDL_ALLOC_PRESENT);
  EXPECT_EQ(result.data[0], 'h');
  EXPECT_EQ(result.data[1], 'e');
  EXPECT_EQ(result.data[2], 'l');
  EXPECT_EQ(result.data[3], 'l');
  EXPECT_EQ(result.data[4], 'o');
  EXPECT_EQ(result.data[5], ' ');
  EXPECT_EQ(result.inline_struct.string2.size, 8);
  EXPECT_EQ(reinterpret_cast<uint64_t>(result.inline_struct.string2.data), FIDL_ALLOC_PRESENT);
  EXPECT_EQ(result.data2[0], 'w');
  EXPECT_EQ(result.data2[1], 'o');
  EXPECT_EQ(result.data2[2], 'r');
  EXPECT_EQ(result.data2[3], 'l');
  EXPECT_EQ(result.data2[4], 'd');
  EXPECT_EQ(result.data2[5], '!');
  EXPECT_EQ(result.data2[6], '!');
  EXPECT_EQ(result.data2[7], '!');
}

TEST(Strings, encode_absent_nonnullable_string_error) {
  unbounded_nonnullable_string_message_layout message = {};
  message.inline_struct.string = fidl_string_t{0u, nullptr};

  const char* error = nullptr;
  uint32_t actual_handles = 0u;
  auto status = fidl_encode(&unbounded_nonnullable_string_message_type, &message, sizeof(message),
                            nullptr, 0, &actual_handles, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
}

TEST(Strings, EncodeIovecEtc_absent_nonnullable_string) {
  unbounded_nonnullable_string_message_layout message = {};
  message.inline_struct.string = fidl_string_t{0u, nullptr};

  uint8_t buf[sizeof(unbounded_nonnullable_string_message_layout)];
  zx_channel_iovec_t iovecs[1];

  const char* error = nullptr;
  uint32_t actual_iovecs = 0u;
  uint32_t actual_handles = 0u;
  auto status = fidl::internal::EncodeIovecEtc<FIDL_WIRE_FORMAT_VERSION_V2>(
      &unbounded_nonnullable_string_message_type, &message, iovecs, std::size(iovecs), nullptr, 0,
      buf, std::size(buf), &actual_iovecs, &actual_handles, &error);

  EXPECT_EQ(status, ZX_OK);
  auto& result = *reinterpret_cast<unbounded_nonnullable_string_message_layout*>(buf);
  EXPECT_EQ(result.inline_struct.string.size, 0);
  EXPECT_EQ(reinterpret_cast<uint64_t>(result.inline_struct.string.data), FIDL_ALLOC_PRESENT);
}

template <Mode mode>
void encode_absent_nullable_string() {
  unbounded_nullable_string_message_layout message = {};
  message.inline_struct.string = fidl_string_t{0u, nullptr};

  uint8_t buf[sizeof(message.inline_struct)];

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&unbounded_nullable_string_message_type, &message, buf, ArrayCount(buf),
                          nullptr, 0, &actual_bytes, &actual_handles, &error);
  auto& result = *reinterpret_cast<decltype(message.inline_struct)*>(buf);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(actual_handles, 0u);
  EXPECT_EQ(reinterpret_cast<uint64_t>(result.string.data), FIDL_ALLOC_ABSENT);
}

template <Mode mode>
void encode_present_nonnullable_bounded_string() {
  bounded_32_nonnullable_string_message_layout message = {};
  message.inline_struct.string = fidl_string_t{6, &message.data[0]};
  memcpy(message.data, "hello!", 6);

  uint8_t buf[sizeof(bounded_32_nonnullable_string_message_layout)];

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&bounded_32_nonnullable_string_message_type, &message, buf,
                          ArrayCount(buf), nullptr, 0, &actual_bytes, &actual_handles, &error);
  auto& result = *reinterpret_cast<bounded_32_nonnullable_string_message_layout*>(buf);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(actual_handles, 0u);
  EXPECT_EQ(result.inline_struct.string.size, 6);
  EXPECT_EQ(reinterpret_cast<uint64_t>(result.inline_struct.string.data), FIDL_ALLOC_PRESENT);
  EXPECT_EQ(result.data[0], 'h');
  EXPECT_EQ(result.data[1], 'e');
  EXPECT_EQ(result.data[2], 'l');
  EXPECT_EQ(result.data[3], 'l');
  EXPECT_EQ(result.data[4], 'o');
  EXPECT_EQ(result.data[5], '!');
}

template <Mode mode>
void encode_present_nullable_bounded_string() {
  bounded_32_nullable_string_message_layout message = {};
  message.inline_struct.string = fidl_string_t{6, &message.data[0]};
  memcpy(message.data, "hello!", 6);

  uint8_t buf[sizeof(message)];

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&bounded_32_nullable_string_message_type, &message, buf, ArrayCount(buf),
                          nullptr, 0, &actual_bytes, &actual_handles, &error);
  auto& result = *reinterpret_cast<bounded_32_nullable_string_message_layout*>(buf);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(actual_handles, 0u);
  EXPECT_EQ(result.inline_struct.string.size, 6);
  EXPECT_EQ(reinterpret_cast<uint64_t>(result.inline_struct.string.data), FIDL_ALLOC_PRESENT);
  EXPECT_EQ(result.data[0], 'h');
  EXPECT_EQ(result.data[1], 'e');
  EXPECT_EQ(result.data[2], 'l');
  EXPECT_EQ(result.data[3], 'l');
  EXPECT_EQ(result.data[4], 'o');
  EXPECT_EQ(result.data[5], '!');
}

template <Mode mode>
void encode_absent_nonnullable_bounded_string_error() {
  bounded_32_nonnullable_string_message_layout message = {};
  message.inline_struct.string = fidl_string_t{6, nullptr};

  uint8_t buf[sizeof(message)];

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&bounded_32_nonnullable_string_message_type, &message, buf,
                          ArrayCount(buf), nullptr, 0, &actual_bytes, &actual_handles, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
  EXPECT_EQ(reinterpret_cast<uint64_t>(message.inline_struct.string.data), FIDL_ALLOC_ABSENT);
}

template <Mode mode>
void encode_absent_nullable_bounded_string() {
  bounded_32_nullable_string_message_layout message = {};
  message.inline_struct.string = fidl_string_t{6, nullptr};

  uint8_t buf[sizeof(message.inline_struct)];

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&bounded_32_nullable_string_message_type, &message, buf, ArrayCount(buf),
                          nullptr, 0, &actual_bytes, &actual_handles, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
}

template <Mode mode>
void encode_present_nonnullable_bounded_string_short_error() {
  multiple_short_nonnullable_strings_message_layout message = {};
  message.inline_struct.string = fidl_string_t{6, &message.data[0]};
  message.inline_struct.string2 = fidl_string_t{6, &message.data2[0]};
  memcpy(message.data, "hello!", 6);
  memcpy(message.data2, "hello!", 6);

  uint8_t buf[sizeof(message)];

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&multiple_short_nonnullable_strings_message_type, &message, buf,
                          ArrayCount(buf), nullptr, 0, &actual_bytes, &actual_handles, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
}

template <Mode mode>
void encode_present_nullable_bounded_string_short_error() {
  multiple_short_nullable_strings_message_layout message = {};
  message.inline_struct.string = fidl_string_t{6, &message.data[0]};
  message.inline_struct.string2 = fidl_string_t{6, &message.data2[0]};
  memcpy(message.data, "hello!", 6);
  memcpy(message.data2, "hello!", 6);

  uint8_t buf[sizeof(message)];

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&multiple_short_nullable_strings_message_type, &message, buf,
                          ArrayCount(buf), nullptr, 0, &actual_bytes, &actual_handles, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
}

template <Mode mode>
void encode_vector_with_huge_count() {
  unbounded_nonnullable_vector_of_uint32_message_layout message = {};
  // (2^30 + 4) * 4 (4 == sizeof(uint32_t)) overflows to 16 when stored as uint32_t.
  // We want 16 because it happens to be the actual size of the vector data in the message,
  // so we can trigger the overflow without triggering the "tried to claim too many bytes" or
  // "didn't use all the bytes in the message" errors.
  message.inline_struct.vector = fidl_vector_t{(1ull << 30) + 4, &message.uint32[0]};

  uint8_t buf[sizeof(message)];

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&unbounded_nonnullable_vector_of_uint32_message_type, &message, buf,
                          ArrayCount(buf), nullptr, 0, &actual_bytes, &actual_handles, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
  const char expected_error_msg[] = "integer overflow calculating vector size";
  EXPECT_STR_EQ(expected_error_msg, error, "wrong error msg");
  EXPECT_EQ(actual_handles, 0u);
}

#ifdef __Fuchsia__
template <Mode mode>
void encode_present_nonnullable_vector_of_handles() {
  unbounded_nonnullable_vector_of_handles_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, &message.handles[0]};
  message.handles[0] = dummy_handle_0;
  message.handles[1] = dummy_handle_1;
  message.handles[2] = dummy_handle_2;
  message.handles[3] = dummy_handle_3;

  uint8_t buf[sizeof(message)];
  zx_handle_t handles[4] = {};

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status = encode_helper<mode>(&unbounded_nonnullable_vector_of_handles_message_type, &message,
                                    buf, ArrayCount(buf), handles, ArrayCount(handles),
                                    &actual_bytes, &actual_handles, &error);
  auto& result = *reinterpret_cast<unbounded_nonnullable_vector_of_handles_message_layout*>(buf);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(actual_handles, 4u);

  auto message_handles = reinterpret_cast<uint64_t>(result.inline_struct.vector.data);
  EXPECT_EQ(message_handles, FIDL_ALLOC_PRESENT);
  EXPECT_EQ(handles[0], dummy_handle_0);
  EXPECT_EQ(handles[1], dummy_handle_1);
  EXPECT_EQ(handles[2], dummy_handle_2);
  EXPECT_EQ(handles[3], dummy_handle_3);
  if (mode == Mode::Iovec1FullyLinearized || mode == Mode::Iovec16) {
    EXPECT_EQ(message.handles[0], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.handles[1], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.handles[2], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.handles[3], ZX_HANDLE_INVALID);
  }
  EXPECT_EQ(result.handles[0], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.handles[1], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.handles[2], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.handles[3], FIDL_HANDLE_PRESENT);
}

template <Mode mode>
void encode_present_nullable_vector_of_handles() {
  unbounded_nullable_vector_of_handles_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, &message.handles[0]};
  message.handles[0] = dummy_handle_0;
  message.handles[1] = dummy_handle_1;
  message.handles[2] = dummy_handle_2;
  message.handles[3] = dummy_handle_3;

  uint8_t buf[sizeof(message)];
  zx_handle_t handles[4] = {};

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status = encode_helper<mode>(&unbounded_nullable_vector_of_handles_message_type, &message,
                                    buf, ArrayCount(buf), handles, ArrayCount(handles),
                                    &actual_bytes, &actual_handles, &error);
  auto& result = *reinterpret_cast<unbounded_nullable_vector_of_handles_message_layout*>(buf);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(actual_handles, 4u);

  auto message_handles = reinterpret_cast<uint64_t>(result.inline_struct.vector.data);
  EXPECT_EQ(message_handles, FIDL_ALLOC_PRESENT);
  EXPECT_EQ(handles[0], dummy_handle_0);
  EXPECT_EQ(handles[1], dummy_handle_1);
  EXPECT_EQ(handles[2], dummy_handle_2);
  EXPECT_EQ(handles[3], dummy_handle_3);
  if (mode == Mode::Iovec1FullyLinearized || mode == Mode::Iovec16) {
    EXPECT_EQ(message.handles[0], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.handles[1], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.handles[2], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.handles[3], ZX_HANDLE_INVALID);
  }
  EXPECT_EQ(result.handles[0], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.handles[1], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.handles[2], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.handles[3], FIDL_HANDLE_PRESENT);
}

template <Mode mode>
void encode_absent_nonnullable_vector_of_handles_error() {
  unbounded_nonnullable_vector_of_handles_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, nullptr};

  uint8_t buf[sizeof(message)];
  zx_handle_t handles[4] = {};

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status = encode_helper<mode>(&unbounded_nonnullable_vector_of_handles_message_type, &message,
                                    buf, ArrayCount(buf), handles, ArrayCount(handles),
                                    &actual_bytes, &actual_handles, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
}

template <Mode mode>
void encode_absent_nullable_vector_of_handles() {
  unbounded_nullable_vector_of_handles_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, nullptr};

  uint8_t buf[sizeof(message.inline_struct)];

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&unbounded_nullable_vector_of_handles_message_type, &message, buf,
                          ArrayCount(buf), nullptr, 0u, &actual_bytes, &actual_handles, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
}

template <Mode mode>
void encode_present_nonnullable_bounded_vector_of_handles() {
  bounded_32_nonnullable_vector_of_handles_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, &message.handles[0]};
  message.handles[0] = dummy_handle_0;
  message.handles[1] = dummy_handle_1;
  message.handles[2] = dummy_handle_2;
  message.handles[3] = dummy_handle_3;
  uint8_t buf[sizeof(message)];

  zx_handle_t handles[4] = {};

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status = encode_helper<mode>(&bounded_32_nonnullable_vector_of_handles_message_type,
                                    &message, buf, ArrayCount(buf), handles, ArrayCount(handles),
                                    &actual_bytes, &actual_handles, &error);
  auto& result = *reinterpret_cast<bounded_32_nonnullable_vector_of_handles_message_layout*>(buf);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(actual_handles, 4u);

  auto message_handles = reinterpret_cast<uint64_t>(result.inline_struct.vector.data);
  EXPECT_EQ(message_handles, FIDL_ALLOC_PRESENT);
  EXPECT_EQ(handles[0], dummy_handle_0);
  EXPECT_EQ(handles[1], dummy_handle_1);
  EXPECT_EQ(handles[2], dummy_handle_2);
  EXPECT_EQ(handles[3], dummy_handle_3);
  if (mode == Mode::Iovec1FullyLinearized || mode == Mode::Iovec16) {
    EXPECT_EQ(message.handles[0], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.handles[1], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.handles[2], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.handles[3], ZX_HANDLE_INVALID);
  }
  EXPECT_EQ(result.handles[0], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.handles[1], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.handles[2], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.handles[3], FIDL_HANDLE_PRESENT);
}

template <Mode mode>
void encode_present_nullable_bounded_vector_of_handles() {
  bounded_32_nullable_vector_of_handles_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, &message.handles[0]};
  message.handles[0] = dummy_handle_0;
  message.handles[1] = dummy_handle_1;
  message.handles[2] = dummy_handle_2;
  message.handles[3] = dummy_handle_3;

  uint8_t buf[sizeof(message)];
  zx_handle_t handles[4] = {};

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status = encode_helper<mode>(&bounded_32_nullable_vector_of_handles_message_type, &message,
                                    buf, ArrayCount(buf), handles, ArrayCount(handles),
                                    &actual_bytes, &actual_handles, &error);
  auto& result = *reinterpret_cast<bounded_32_nullable_vector_of_handles_message_layout*>(buf);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(actual_handles, 4u);

  auto message_handles = reinterpret_cast<uint64_t>(result.inline_struct.vector.data);
  EXPECT_EQ(message_handles, FIDL_ALLOC_PRESENT);
  EXPECT_EQ(handles[0], dummy_handle_0);
  EXPECT_EQ(handles[1], dummy_handle_1);
  EXPECT_EQ(handles[2], dummy_handle_2);
  EXPECT_EQ(handles[3], dummy_handle_3);
  if (mode == Mode::Iovec1FullyLinearized || mode == Mode::Iovec16) {
    EXPECT_EQ(message.handles[0], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.handles[1], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.handles[2], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.handles[3], ZX_HANDLE_INVALID);
  }
  EXPECT_EQ(result.handles[0], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.handles[1], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.handles[2], FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.handles[3], FIDL_HANDLE_PRESENT);
}

template <Mode mode>
void encode_absent_nonnullable_bounded_vector_of_handles() {
  bounded_32_nonnullable_vector_of_handles_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, nullptr};

  uint8_t buf[sizeof(message)];
  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&bounded_32_nonnullable_vector_of_handles_message_type, &message, buf,
                          ArrayCount(buf), nullptr, 0u, &actual_bytes, &actual_handles, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
}

template <Mode mode>
void encode_absent_nullable_bounded_vector_of_handles() {
  bounded_32_nullable_vector_of_handles_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, nullptr};

  uint8_t buf[sizeof(message.inline_struct)];
  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&bounded_32_nullable_vector_of_handles_message_type, &message, buf,
                          ArrayCount(buf), nullptr, 0u, &actual_bytes, &actual_handles, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
}

template <Mode mode>
void encode_present_nonnullable_bounded_vector_of_handles_short_error() {
  multiple_nonnullable_vectors_of_handles_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, &message.handles[0]};
  message.inline_struct.vector2 = fidl_vector_t{4, &message.handles2[0]};
  message.handles[0] = dummy_handle_0;
  message.handles[1] = dummy_handle_1;
  message.handles[2] = dummy_handle_2;
  message.handles[3] = dummy_handle_3;
  message.handles2[0] = dummy_handle_4;
  message.handles2[1] = dummy_handle_5;
  message.handles2[2] = dummy_handle_6;
  message.handles2[3] = dummy_handle_7;

  uint8_t buf[sizeof(message)];
  zx_handle_t handles[8] = {};

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status = encode_helper<mode>(&multiple_nonnullable_vectors_of_handles_message_type, &message,
                                    buf, ArrayCount(buf), handles, ArrayCount(handles),
                                    &actual_bytes, &actual_handles, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
}

template <Mode mode>
void encode_present_nullable_bounded_vector_of_handles_short_error() {
  multiple_nullable_vectors_of_handles_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, &message.handles[0]};
  message.inline_struct.vector2 = fidl_vector_t{4, &message.handles2[0]};
  message.handles[0] = dummy_handle_0;
  message.handles[1] = dummy_handle_1;
  message.handles[2] = dummy_handle_2;
  message.handles[3] = dummy_handle_3;
  message.handles2[0] = dummy_handle_4;
  message.handles2[1] = dummy_handle_5;
  message.handles2[2] = dummy_handle_6;
  message.handles2[3] = dummy_handle_7;

  uint8_t buf[sizeof(message)];
  zx_handle_t handles[8] = {};

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status = encode_helper<mode>(&multiple_nullable_vectors_of_handles_message_type, &message,
                                    buf, ArrayCount(buf), handles, ArrayCount(handles),
                                    &actual_bytes, &actual_handles, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
}
#endif

template <Mode mode>
void encode_present_nonnullable_vector_of_uint32() {
  unbounded_nonnullable_vector_of_uint32_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, &message.uint32[0]};

  uint8_t buf[sizeof(message)];
  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&unbounded_nonnullable_vector_of_uint32_message_type, &message, buf,
                          ArrayCount(buf), nullptr, 0, &actual_bytes, &actual_handles, &error);
  auto& result = *reinterpret_cast<unbounded_nonnullable_vector_of_uint32_message_layout*>(buf);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(actual_handles, 0u);

  auto result_uint32 = reinterpret_cast<uint64_t>(result.inline_struct.vector.data);
  EXPECT_EQ(result_uint32, FIDL_ALLOC_PRESENT);
}

template <Mode mode>
void encode_present_nullable_vector_of_uint32() {
  unbounded_nullable_vector_of_uint32_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, &message.uint32[0]};

  uint8_t buf[sizeof(message)];
  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&unbounded_nullable_vector_of_uint32_message_type, &message, buf,
                          ArrayCount(buf), nullptr, 0, &actual_bytes, &actual_handles, &error);
  auto& result = *reinterpret_cast<unbounded_nullable_vector_of_uint32_message_layout*>(buf);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(actual_handles, 0u);

  auto result_uint32 = reinterpret_cast<uint64_t>(result.inline_struct.vector.data);
  EXPECT_EQ(result_uint32, FIDL_ALLOC_PRESENT);
}

template <Mode mode>
void encode_absent_nonnullable_vector_of_uint32_error() {
  unbounded_nonnullable_vector_of_uint32_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, nullptr};

  uint8_t buf[sizeof(message)];
  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&unbounded_nonnullable_vector_of_uint32_message_type, &message, buf,
                          ArrayCount(buf), nullptr, 0, &actual_bytes, &actual_handles, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
}

TEST(Vectors, encode_absent_and_empty_nonnullable_vector_of_uint32_error) {
  unbounded_nonnullable_vector_of_uint32_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{0, nullptr};

  const char* error = nullptr;
  uint32_t actual_handles = 0u;
  auto status = fidl_encode(&unbounded_nonnullable_vector_of_uint32_message_type, &message,
                            sizeof(message.inline_struct), nullptr, 0, &actual_handles, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
}

TEST(Vectors, EncodeIovecEtc_absent_and_empty_nonnullable_vector_of_uint32) {
  unbounded_nonnullable_vector_of_uint32_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{0, nullptr};

  zx_channel_iovec_t iovecs[1];
  uint8_t buf[sizeof(message.inline_struct)];
  const char* error = nullptr;
  uint32_t actual_iovecs = 0u;
  uint32_t actual_handles = 0u;
  auto status = fidl::internal::EncodeIovecEtc<FIDL_WIRE_FORMAT_VERSION_V2>(
      &unbounded_nonnullable_vector_of_uint32_message_type, &message, iovecs, std::size(iovecs),
      nullptr, 0, buf, std::size(buf), &actual_iovecs, &actual_handles, &error);
  auto& result = *reinterpret_cast<decltype(message.inline_struct)*>(buf);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(result.vector.count, 0);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(result.vector.data), FIDL_ALLOC_PRESENT);
}

template <Mode mode>
void encode_absent_nullable_vector_of_uint32() {
  unbounded_nullable_vector_of_uint32_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{0, nullptr};

  uint8_t buf[sizeof(message.inline_struct)];
  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&unbounded_nullable_vector_of_uint32_message_type, &message, buf,
                          ArrayCount(buf), nullptr, 0, &actual_bytes, &actual_handles, &error);
  auto& result = *reinterpret_cast<decltype(message.inline_struct)*>(buf);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error);

  auto result_uint32 = reinterpret_cast<uint64_t>(result.vector.data);
  EXPECT_EQ(result_uint32, FIDL_ALLOC_ABSENT);
}

template <Mode mode>
void encode_absent_nullable_vector_of_uint32_non_zero_length_error() {
  unbounded_nullable_vector_of_uint32_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, nullptr};

  uint8_t buf[sizeof(message.inline_struct)];
  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&unbounded_nullable_vector_of_uint32_message_type, &message, buf,
                          ArrayCount(buf), nullptr, 0u, &actual_bytes, &actual_handles, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
}

template <Mode mode>
void encode_present_nonnullable_bounded_vector_of_uint32() {
  bounded_32_nonnullable_vector_of_uint32_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, &message.uint32[0]};

  uint8_t buf[sizeof(message)];
  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&bounded_32_nonnullable_vector_of_uint32_message_type, &message, buf,
                          ArrayCount(buf), nullptr, 0, &actual_bytes, &actual_handles, &error);
  auto& result = *reinterpret_cast<bounded_32_nonnullable_vector_of_uint32_message_layout*>(buf);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(actual_handles, 0u);

  auto result_uint32 = reinterpret_cast<uint64_t>(result.inline_struct.vector.data);
  EXPECT_EQ(result_uint32, FIDL_ALLOC_PRESENT);
}

template <Mode mode>
void encode_present_nullable_bounded_vector_of_uint32() {
  bounded_32_nullable_vector_of_uint32_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, &message.uint32[0]};

  uint8_t buf[sizeof(message)];
  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&bounded_32_nullable_vector_of_uint32_message_type, &message, buf,
                          ArrayCount(buf), nullptr, 0, &actual_bytes, &actual_handles, &error);
  auto& result = *reinterpret_cast<bounded_32_nullable_vector_of_uint32_message_layout*>(buf);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(actual_handles, 0u);

  auto result_uint32 = reinterpret_cast<uint64_t>(result.inline_struct.vector.data);
  EXPECT_EQ(result_uint32, FIDL_ALLOC_PRESENT);
}

template <Mode mode>
void encode_absent_nonnullable_bounded_vector_of_uint32() {
  bounded_32_nonnullable_vector_of_uint32_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, nullptr};

  uint8_t buf[sizeof(message)];
  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&bounded_32_nonnullable_vector_of_uint32_message_type, &message, buf,
                          ArrayCount(buf), nullptr, 0u, &actual_bytes, &actual_handles, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
}

template <Mode mode>
void encode_absent_nullable_bounded_vector_of_uint32() {
  bounded_32_nullable_vector_of_uint32_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, nullptr};

  uint8_t buf[sizeof(message.inline_struct)];
  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&bounded_32_nullable_vector_of_uint32_message_type, &message, buf,
                          ArrayCount(buf), nullptr, 0u, &actual_bytes, &actual_handles, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
}

template <Mode mode>
void encode_present_nonnullable_bounded_vector_of_uint32_short_error() {
  multiple_nonnullable_vectors_of_uint32_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, &message.uint32[0]};
  message.inline_struct.vector2 = fidl_vector_t{4, &message.uint32_2[0]};

  uint8_t buf[sizeof(message)];
  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&multiple_nonnullable_vectors_of_uint32_message_type, &message, buf,
                          ArrayCount(buf), nullptr, 0, &actual_bytes, &actual_handles, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
}

template <Mode mode>
void encode_present_nullable_bounded_vector_of_uint32_short_error() {
  multiple_nullable_vectors_of_uint32_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{4, &message.uint32[0]};
  message.inline_struct.vector2 = fidl_vector_t{4, &message.uint32_2[0]};

  uint8_t buf[sizeof(message)];
  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&multiple_nullable_vectors_of_uint32_message_type, &message, buf,
                          ArrayCount(buf), nullptr, 0, &actual_bytes, &actual_handles, &error);

  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(error);
}

#if __Fuchsia__
template <Mode mode>
void encode_nested_nonnullable_structs() {
  // Note the traversal order! l1 -> l3 -> l2 -> l0
  nested_structs_message_layout message = {};
  message.inline_struct.l0.l1.handle_1 = dummy_handle_0;
  message.inline_struct.l0.l1.l2.l3.handle_3 = dummy_handle_1;
  message.inline_struct.l0.l1.l2.handle_2 = dummy_handle_2;
  message.inline_struct.l0.handle_0 = dummy_handle_3;

  uint8_t buf[sizeof(message)];
  zx_handle_t handles[4] = {};

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&nested_structs_message_type, &message, buf, ArrayCount(buf), handles,
                          ArrayCount(handles), &actual_bytes, &actual_handles, &error);
  auto& result = *reinterpret_cast<nested_structs_message_layout*>(buf);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);

  if (mode == Mode::Iovec1FullyLinearized || mode == Mode::Iovec16) {
    EXPECT_EQ(message.inline_struct.l0.l1.handle_1, ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.l0.l1.l2.l3.handle_3, ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.l0.l1.l2.handle_2, ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.l0.handle_0, ZX_HANDLE_INVALID);
  }
  EXPECT_EQ(result.inline_struct.l0.l1.handle_1, FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.inline_struct.l0.l1.l2.l3.handle_3, FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.inline_struct.l0.l1.l2.handle_2, FIDL_HANDLE_PRESENT);
  EXPECT_EQ(result.inline_struct.l0.handle_0, FIDL_HANDLE_PRESENT);

  EXPECT_EQ(handles[0], dummy_handle_0);
  EXPECT_EQ(handles[1], dummy_handle_1);
  EXPECT_EQ(handles[2], dummy_handle_2);
  EXPECT_EQ(handles[3], dummy_handle_3);
}

template <Mode mode>
void encode_nested_nonnullable_structs_zero_padding() {
  // Initialize a buffer with garbage value of 0xAA.
  constexpr size_t kBufferSize = sizeof(nested_structs_message_layout);
  uint8_t buffer[kBufferSize];
  memset(buffer, 0xAA, sizeof(buffer));

  nested_structs_message_layout* message = new (&buffer[0]) nested_structs_message_layout;
  message->inline_struct.l0.l1.handle_1 = dummy_handle_0;
  message->inline_struct.l0.l1.l2.l3.handle_3 = dummy_handle_1;
  message->inline_struct.l0.l1.l2.handle_2 = dummy_handle_2;
  message->inline_struct.l0.handle_0 = dummy_handle_3;

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

  // Read padding bytes, four bytes at a time.
  uint32_t* dwords = reinterpret_cast<uint32_t*>(&buffer[0]);
  EXPECT_EQ(dwords[7], 0xAAAAAAAA);
  EXPECT_EQ(dwords[13], 0xAAAAAAAA);
  EXPECT_EQ(dwords[17], 0xAAAAAAAA);

  uint8_t out_buf[kBufferSize];
  uint32_t* out_dwords = reinterpret_cast<uint32_t*>(&out_buf);
  out_dwords[7] = 0xBBBBBBBB;
  out_dwords[13] = 0xBBBBBBBB;
  out_dwords[17] = 0xBBBBBBBB;
  zx_handle_t handles[4] = {};

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&nested_structs_message_type, message, out_buf, ArrayCount(out_buf),
                          handles, ArrayCount(handles), &actual_bytes, &actual_handles, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);

  // Validate that all padding bytes are zero, by checking four bytes at a time.
  EXPECT_EQ(out_dwords[7], 0);
  EXPECT_EQ(out_dwords[13], 0);
  EXPECT_EQ(out_dwords[17], 0);
}

template <Mode mode>
void encode_nested_nullable_structs() {
  nested_struct_ptrs_message_layout message = {};
  message.inline_struct.l0_inline.l1_inline.l2_present = &message.in_in_out_2;
  message.inline_struct.l0_inline.l1_inline.l2_present->l3_present = &message.in_in_out_out_3;
  message.inline_struct.l0_inline.l1_inline.l2_inline.l3_present = &message.in_in_in_out_3;
  message.inline_struct.l0_inline.l1_present = &message.in_out_1;
  message.inline_struct.l0_inline.l1_present->l2_present = &message.in_out_out_2;
  message.inline_struct.l0_inline.l1_present->l2_present->l3_present = &message.in_out_out_out_3;
  message.inline_struct.l0_inline.l1_present->l2_inline.l3_present = &message.in_out_in_out_3;
  message.inline_struct.l0_present = &message.out_0;
  message.inline_struct.l0_present->l1_inline.l2_present = &message.out_in_out_2;
  message.inline_struct.l0_present->l1_inline.l2_present->l3_present = &message.out_in_out_out_3;
  message.inline_struct.l0_present->l1_inline.l2_inline.l3_present = &message.out_in_in_out_3;
  message.inline_struct.l0_present->l1_present = &message.out_out_1;
  message.inline_struct.l0_present->l1_present->l2_present = &message.out_out_out_2;
  message.inline_struct.l0_present->l1_present->l2_present->l3_present = &message.out_out_out_out_3;
  message.inline_struct.l0_present->l1_present->l2_inline.l3_present = &message.out_out_in_out_3;

  // 0 inline
  //     1 inline
  //         handle
  message.inline_struct.l0_inline.l1_inline.handle_1 = dummy_handle_0;
  //         2 out of line
  //             3 out of line
  message.in_in_out_out_3.handle_3 = dummy_handle_1;
  //             3 inline
  message.in_in_out_2.l3_inline.handle_3 = dummy_handle_2;
  //             handle
  message.in_in_out_2.handle_2 = dummy_handle_3;
  //         2 inline
  //             3 out of line
  message.in_in_in_out_3.handle_3 = dummy_handle_4;
  //             3 inline
  message.inline_struct.l0_inline.l1_inline.l2_inline.l3_inline.handle_3 = dummy_handle_5;
  //             handle
  message.inline_struct.l0_inline.l1_inline.l2_inline.handle_2 = dummy_handle_6;
  //     handle
  message.inline_struct.l0_inline.handle_0 = dummy_handle_7;
  //     1 out of line
  //         handle
  message.in_out_1.handle_1 = dummy_handle_8;
  //         2 out of line
  //             3 out of line
  message.in_out_out_out_3.handle_3 = dummy_handle_9;
  //             3 inline
  message.in_out_out_2.l3_inline.handle_3 = dummy_handle_10;
  //             handle
  message.in_out_out_2.handle_2 = dummy_handle_11;
  //         2 inline
  //             3 out of line
  message.in_out_in_out_3.handle_3 = dummy_handle_12;
  //             3 inline
  message.in_out_1.l2_inline.l3_inline.handle_3 = dummy_handle_13;
  //             handle
  message.in_out_1.l2_inline.handle_2 = dummy_handle_14;
  // 0 out of line
  //     1 inline
  //         handle
  message.out_0.l1_inline.handle_1 = dummy_handle_15;
  //         2 out of line
  //             3 out of line
  message.out_in_out_out_3.handle_3 = dummy_handle_16;
  //             3 inline
  message.out_in_out_2.l3_inline.handle_3 = dummy_handle_17;
  //             handle
  message.out_in_out_2.handle_2 = dummy_handle_18;
  //         2 inline
  //             3 out of line
  message.out_in_in_out_3.handle_3 = dummy_handle_19;
  //             3 inline
  message.out_0.l1_inline.l2_inline.l3_inline.handle_3 = dummy_handle_20;
  //             handle
  message.out_0.l1_inline.l2_inline.handle_2 = dummy_handle_21;
  //     handle
  message.out_0.handle_0 = dummy_handle_22;
  //     1 out of line
  //         handle
  message.out_out_1.handle_1 = dummy_handle_23;
  //         2 out of line
  //             3 out of line
  message.out_out_out_out_3.handle_3 = dummy_handle_24;
  //             3 inline
  message.out_out_out_2.l3_inline.handle_3 = dummy_handle_25;
  //             handle
  message.out_out_out_2.handle_2 = dummy_handle_26;
  //         2 inline
  //             3 out of line
  message.out_out_in_out_3.handle_3 = dummy_handle_27;
  //             3 inline
  message.out_out_1.l2_inline.l3_inline.handle_3 = dummy_handle_28;
  //             handle
  message.out_out_1.l2_inline.handle_2 = dummy_handle_29;

  uint8_t buf[sizeof(message)];
  zx_handle_t handles[30] = {};

  const char* error = nullptr;
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  auto status =
      encode_helper<mode>(&nested_struct_ptrs_message_type, &message, buf, ArrayCount(buf), handles,
                          ArrayCount(handles), &actual_bytes, &actual_handles, &error);
  auto& result = *reinterpret_cast<nested_struct_ptrs_message_layout*>(buf);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);

  EXPECT_EQ(handles[0], dummy_handle_0);
  EXPECT_EQ(handles[1], dummy_handle_1);
  EXPECT_EQ(handles[2], dummy_handle_2);
  EXPECT_EQ(handles[3], dummy_handle_3);
  EXPECT_EQ(handles[4], dummy_handle_4);
  EXPECT_EQ(handles[5], dummy_handle_5);
  EXPECT_EQ(handles[6], dummy_handle_6);
  EXPECT_EQ(handles[7], dummy_handle_7);
  EXPECT_EQ(handles[8], dummy_handle_8);
  EXPECT_EQ(handles[9], dummy_handle_9);
  EXPECT_EQ(handles[10], dummy_handle_10);
  EXPECT_EQ(handles[11], dummy_handle_11);
  EXPECT_EQ(handles[12], dummy_handle_12);
  EXPECT_EQ(handles[13], dummy_handle_13);
  EXPECT_EQ(handles[14], dummy_handle_14);
  EXPECT_EQ(handles[15], dummy_handle_15);
  EXPECT_EQ(handles[16], dummy_handle_16);
  EXPECT_EQ(handles[17], dummy_handle_17);
  EXPECT_EQ(handles[18], dummy_handle_18);
  EXPECT_EQ(handles[19], dummy_handle_19);
  EXPECT_EQ(handles[20], dummy_handle_20);
  EXPECT_EQ(handles[21], dummy_handle_21);
  EXPECT_EQ(handles[22], dummy_handle_22);
  EXPECT_EQ(handles[23], dummy_handle_23);
  EXPECT_EQ(handles[24], dummy_handle_24);
  EXPECT_EQ(handles[25], dummy_handle_25);
  EXPECT_EQ(handles[26], dummy_handle_26);
  EXPECT_EQ(handles[27], dummy_handle_27);
  EXPECT_EQ(handles[28], dummy_handle_28);
  EXPECT_EQ(handles[29], dummy_handle_29);

  // Finally, check that all absent members are FIDL_ALLOC_ABSENT.
  EXPECT_EQ(reinterpret_cast<uintptr_t>(result.inline_struct.l0_absent), FIDL_ALLOC_ABSENT);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(result.inline_struct.l0_inline.l1_absent),
            FIDL_ALLOC_ABSENT);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(result.inline_struct.l0_inline.l1_inline.l2_absent),
            FIDL_ALLOC_ABSENT);
  EXPECT_EQ(
      reinterpret_cast<uintptr_t>(result.inline_struct.l0_inline.l1_inline.l2_inline.l3_absent),
      FIDL_ALLOC_ABSENT);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(result.in_in_out_2.l3_absent), FIDL_ALLOC_ABSENT);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(result.in_out_1.l2_absent), FIDL_ALLOC_ABSENT);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(result.in_out_1.l2_inline.l3_absent), FIDL_ALLOC_ABSENT);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(result.in_out_out_2.l3_absent), FIDL_ALLOC_ABSENT);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(result.out_0.l1_absent), FIDL_ALLOC_ABSENT);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(result.out_0.l1_inline.l2_absent), FIDL_ALLOC_ABSENT);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(result.out_0.l1_inline.l2_inline.l3_absent),
            FIDL_ALLOC_ABSENT);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(result.out_in_out_2.l3_absent), FIDL_ALLOC_ABSENT);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(result.out_out_1.l2_absent), FIDL_ALLOC_ABSENT);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(result.out_out_1.l2_inline.l3_absent), FIDL_ALLOC_ABSENT);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(result.out_out_out_2.l3_absent), FIDL_ALLOC_ABSENT);
}
#endif

TEST(TrackingPtr, encode_string_view_with_fidl_allocator) {
  fidl::Arena allocator;
  const char input[] = "abcd";
  StringStruct str = {.str = fidl::StringView(allocator, input)};

  zx_channel_iovec_t iovecs[1];
  constexpr uint32_t kBufSize = 512;
  uint8_t buffer[kBufSize];
  uint32_t actual_iovecs = 0u;
  uint32_t actual_handles = 0u;
  const char* error = nullptr;
  auto status = fidl::internal::EncodeIovecEtc<FIDL_WIRE_FORMAT_VERSION_V2>(
      &fidl_test_coding_StringStructTable, &str, iovecs, 1, nullptr, 0, buffer, kBufSize,
      &actual_iovecs, &actual_handles, &error);
  EXPECT_EQ(status, ZX_OK);

  fidl_string_t* written_string = reinterpret_cast<fidl_string_t*>(buffer);

  ASSERT_EQ(actual_iovecs, 1);
  EXPECT_EQ(iovecs[0].capacity, 24);
  EXPECT_EQ(actual_handles, 0);
  EXPECT_EQ(written_string->size, strlen(input));
  EXPECT_EQ(reinterpret_cast<uintptr_t>(written_string->data), FIDL_ALLOC_PRESENT);
  const char* written_data = reinterpret_cast<char*>(buffer + sizeof(fidl_string_t));
  for (size_t i = 0; i < strlen(input); i++)
    EXPECT_EQ(written_data[i], input[i]);
}

struct BoolStruct {
  bool v;
};

#if __Fuchsia__

TEST(EncodeIovecEtc, single_present_handle_disposition) {
  nonnullable_handle_message_layout message = {};
  message.inline_struct.handle = dummy_handle_0;

  zx_channel_iovec_t iovecs[1];
  uint8_t buf[sizeof(message)];
  zx_handle_disposition_t handle_dispositions[1] = {};

  const char* error = nullptr;
  uint32_t actual_iovecs = 0u;
  uint32_t actual_handles = 0u;
  auto status = fidl::internal::EncodeIovecEtc<FIDL_WIRE_FORMAT_VERSION_V2>(
      &nonnullable_channel_message_type, &message, iovecs, std::size(iovecs), handle_dispositions,
      std::size(handle_dispositions), buf, std::size(buf), &actual_iovecs, &actual_handles, &error);
  auto& result = *reinterpret_cast<nonnullable_handle_message_layout*>(buf);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(actual_handles, 1u);
  EXPECT_EQ(handle_dispositions[0].operation, ZX_HANDLE_OP_MOVE);
  EXPECT_EQ(handle_dispositions[0].handle, dummy_handle_0);
  EXPECT_EQ(handle_dispositions[0].type, ZX_OBJ_TYPE_CHANNEL);
  EXPECT_EQ(handle_dispositions[0].rights, ZX_RIGHT_READ | ZX_RIGHT_WRITE);
  EXPECT_EQ(handle_dispositions[0].result, ZX_OK);
  EXPECT_EQ(message.inline_struct.handle, ZX_HANDLE_INVALID);
  EXPECT_EQ(result.inline_struct.handle, FIDL_HANDLE_PRESENT);
}

TEST(FidlLinearizeAndEncodeEtc, encode_single_present_handle_disposition) {
  nonnullable_handle_message_layout message = {};
  message.inline_struct.handle = dummy_handle_0;

  zx_handle_disposition_t handle_dispositions[1] = {};

  const char* error = nullptr;
  uint32_t actual_handles = 0u;
  auto status = fidl_encode_etc(&nonnullable_channel_message_type, &message, sizeof(message),
                                handle_dispositions, ArrayCount(handle_dispositions),
                                &actual_handles, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  EXPECT_EQ(actual_handles, 1u);
  EXPECT_EQ(handle_dispositions[0].operation, ZX_HANDLE_OP_MOVE);
  EXPECT_EQ(handle_dispositions[0].handle, dummy_handle_0);
  EXPECT_EQ(handle_dispositions[0].type, ZX_OBJ_TYPE_CHANNEL);
  EXPECT_EQ(handle_dispositions[0].rights, ZX_RIGHT_READ | ZX_RIGHT_WRITE);
  EXPECT_EQ(handle_dispositions[0].result, ZX_OK);
  EXPECT_EQ(message.inline_struct.handle, FIDL_HANDLE_PRESENT);
}
#endif

#if __Fuchsia__
TEST(Handles, encode_single_present_handle_Mode_EncodeOnly) {
  encode_single_present_handle<Mode::EncodeOnly>();
}
TEST(Handles, encode_single_present_handle_zero_trailing_padding_Mode_EncodeOnly) {
  encode_single_present_handle_zero_trailing_padding<Mode::EncodeOnly>();
}
TEST(Handles, encode_multiple_present_handles_Mode_EncodeOnly) {
  encode_multiple_present_handles<Mode::EncodeOnly>();
}
TEST(Handles, encode_single_absent_handle_Mode_EncodeOnly) {
  encode_single_absent_handle<Mode::EncodeOnly>();
}
TEST(Handles, encode_multiple_absent_handles_Mode_EncodeOnly) {
  encode_multiple_absent_handles<Mode::EncodeOnly>();
}
TEST(Handles, encode_single_present_handle_Mode_Iovec1FullyLinearized) {
  encode_single_present_handle<Mode::Iovec1FullyLinearized>();
}
TEST(Handles, encode_single_present_handle_zero_trailing_padding_Mode_Iovec1FullyLinearized) {
  encode_single_present_handle_zero_trailing_padding<Mode::Iovec1FullyLinearized>();
}
TEST(Handles, encode_multiple_present_handles_Mode_Iovec1FullyLinearized) {
  encode_multiple_present_handles<Mode::Iovec1FullyLinearized>();
}
TEST(Handles, encode_single_absent_handle_Mode_Iovec1FullyLinearized) {
  encode_single_absent_handle<Mode::Iovec1FullyLinearized>();
}
TEST(Handles, encode_multiple_absent_handles_Mode_Iovec1FullyLinearized) {
  encode_multiple_absent_handles<Mode::Iovec1FullyLinearized>();
}
TEST(Handles, encode_single_present_handle_Mode_Iovec16) {
  encode_single_present_handle<Mode::Iovec16>();
}
TEST(Handles, encode_single_present_handle_zero_trailing_padding_Mode_Iovec16) {
  encode_single_present_handle_zero_trailing_padding<Mode::Iovec16>();
}
TEST(Handles, encode_multiple_present_handles_Mode_Iovec16) {
  encode_multiple_present_handles<Mode::Iovec16>();
}
TEST(Handles, encode_single_absent_handle_Mode_Iovec16) {
  encode_single_absent_handle<Mode::Iovec16>();
}
TEST(Handles, encode_multiple_absent_handles_Mode_Iovec16) {
  encode_multiple_absent_handles<Mode::Iovec16>();
}

TEST(Arrays, encode_array_of_present_handles_Mode_EncodeOnly) {
  encode_array_of_present_handles<Mode::EncodeOnly>();
}
TEST(Arrays, encode_array_of_nullable_handles_Mode_EncodeOnly) {
  encode_array_of_nullable_handles<Mode::EncodeOnly>();
}
TEST(Arrays, encode_array_of_nullable_handles_with_insufficient_handles_error_Mode_EncodeOnly) {
  encode_array_of_nullable_handles_with_insufficient_handles_error<Mode::EncodeOnly>();
}
TEST(Arrays, encode_array_of_array_of_present_handles_Mode_EncodeOnly) {
  encode_array_of_array_of_present_handles<Mode::EncodeOnly>();
}
TEST(Arrays, encode_out_of_line_array_of_nonnullable_handles_Mode_EncodeOnly) {
  encode_out_of_line_array_of_nonnullable_handles<Mode::EncodeOnly>();
}

TEST(Arrays, encode_array_of_present_handles_error_closes_handles_Mode_EncodeOnly) {
  encode_array_of_present_handles_error_closes_handles<Mode::EncodeOnly>();
}

TEST(Arrays, encode_array_of_present_handles_Mode_Iovec1FullyLinearized) {
  encode_array_of_present_handles<Mode::Iovec1FullyLinearized>();
}
TEST(Arrays, encode_array_of_nullable_handles_Mode_Iovec1FullyLinearized) {
  encode_array_of_nullable_handles<Mode::Iovec1FullyLinearized>();
}
TEST(Arrays,
     encode_array_of_nullable_handles_with_insufficient_handles_error_Mode_Iovec1FullyLinearized) {
  encode_array_of_nullable_handles_with_insufficient_handles_error<Mode::Iovec1FullyLinearized>();
}
TEST(Arrays, encode_array_of_array_of_present_handles_Mode_Iovec1FullyLinearized) {
  encode_array_of_array_of_present_handles<Mode::Iovec1FullyLinearized>();
}
TEST(Arrays, encode_out_of_line_array_of_nonnullable_handles_Mode_Iovec1FullyLinearized) {
  encode_out_of_line_array_of_nonnullable_handles<Mode::Iovec1FullyLinearized>();
}
TEST(Arrays, encode_array_of_present_handles_error_closes_handles_Mode_Iovec1FullyLinearized) {
  encode_array_of_present_handles_error_closes_handles<Mode::Iovec1FullyLinearized>();
}

TEST(Arrays, encode_array_of_present_handles_Mode_Iovec16) {
  encode_array_of_present_handles<Mode::Iovec16>();
}
TEST(Arrays, encode_array_of_nullable_handles_Mode_Iovec16) {
  encode_array_of_nullable_handles<Mode::Iovec16>();
}
TEST(Arrays, encode_array_of_nullable_handles_with_insufficient_handles_error_Mode_Iovec16) {
  encode_array_of_nullable_handles_with_insufficient_handles_error<Mode::Iovec16>();
}
TEST(Arrays, encode_array_of_array_of_present_handles_Mode_Iovec16) {
  encode_array_of_array_of_present_handles<Mode::Iovec16>();
}
TEST(Arrays, encode_out_of_line_array_of_nonnullable_handles_Mode_Iovec16) {
  encode_out_of_line_array_of_nonnullable_handles<Mode::Iovec16>();
}
TEST(Arrays, encode_array_of_present_handles_error_closes_handles_Mode_Iovec16) {
  encode_array_of_present_handles_error_closes_handles<Mode::Iovec16>();
}
#endif

TEST(Strings, encode_present_nonnullable_string_Mode_EncodeOnly) {
  encode_present_nonnullable_string<Mode::EncodeOnly>();
}
TEST(Strings, encode_multiple_present_nullable_string_Mode_EncodeOnly) {
  encode_multiple_present_nullable_string<Mode::EncodeOnly>();
}
TEST(Strings, encode_present_nullable_string_Mode_EncodeOnly) {
  encode_present_nullable_string<Mode::EncodeOnly>();
}
TEST(Strings, encode_absent_nullable_string_Mode_EncodeOnly) {
  encode_absent_nullable_string<Mode::EncodeOnly>();
}
TEST(Strings, encode_present_nonnullable_bounded_string_Mode_EncodeOnly) {
  encode_present_nonnullable_bounded_string<Mode::EncodeOnly>();
}
TEST(Strings, encode_present_nullable_bounded_string_Mode_EncodeOnly) {
  encode_present_nullable_bounded_string<Mode::EncodeOnly>();
}
TEST(Strings, encode_absent_nonnullable_bounded_string_error_Mode_EncodeOnly) {
  encode_absent_nonnullable_bounded_string_error<Mode::EncodeOnly>();
}
TEST(Strings, encode_absent_nullable_bounded_string_Mode_EncodeOnly) {
  encode_absent_nullable_bounded_string<Mode::EncodeOnly>();
}
TEST(Strings, encode_present_nonnullable_bounded_string_short_error_Mode_EncodeOnly) {
  encode_present_nonnullable_bounded_string_short_error<Mode::EncodeOnly>();
}
TEST(Strings, encode_present_nullable_bounded_string_short_error_Mode_EncodeOnly) {
  encode_present_nullable_bounded_string_short_error<Mode::EncodeOnly>();
}
TEST(Strings, encode_present_nonnullable_string_Mode_Iovec1FullyLinearized) {
  encode_present_nonnullable_string<Mode::Iovec1FullyLinearized>();
}
TEST(Strings, encode_multiple_present_nullable_string_Mode_Iovec1FullyLinearized) {
  encode_multiple_present_nullable_string<Mode::Iovec1FullyLinearized>();
}
TEST(Strings, encode_present_nullable_string_Mode_Iovec1FullyLinearized) {
  encode_present_nullable_string<Mode::Iovec1FullyLinearized>();
}
TEST(Strings, encode_absent_nullable_string_Mode_Iovec1FullyLinearized) {
  encode_absent_nullable_string<Mode::Iovec1FullyLinearized>();
}
TEST(Strings, encode_present_nonnullable_bounded_string_Mode_Iovec1FullyLinearized) {
  encode_present_nonnullable_bounded_string<Mode::Iovec1FullyLinearized>();
}
TEST(Strings, encode_present_nullable_bounded_string_Mode_Iovec1FullyLinearized) {
  encode_present_nullable_bounded_string<Mode::Iovec1FullyLinearized>();
}
TEST(Strings, encode_absent_nonnullable_bounded_string_error_Mode_Iovec1FullyLinearized) {
  encode_absent_nonnullable_bounded_string_error<Mode::Iovec1FullyLinearized>();
}
TEST(Strings, encode_absent_nullable_bounded_string_Mode_Iovec1FullyLinearized) {
  encode_absent_nullable_bounded_string<Mode::Iovec1FullyLinearized>();
}
TEST(Strings, encode_present_nonnullable_bounded_string_short_error_Mode_Iovec1FullyLinearized) {
  encode_present_nonnullable_bounded_string_short_error<Mode::Iovec1FullyLinearized>();
}
TEST(Strings, encode_present_nullable_bounded_string_short_error_Mode_Iovec1FullyLinearized) {
  encode_present_nullable_bounded_string_short_error<Mode::Iovec1FullyLinearized>();
}
TEST(Strings, encode_present_nonnullable_string_Mode_Iovec16) {
  encode_present_nonnullable_string<Mode::Iovec16>();
}
TEST(Strings, encode_multiple_present_nullable_string_Mode_Iovec16) {
  encode_multiple_present_nullable_string<Mode::Iovec16>();
}
TEST(Strings, encode_present_nullable_string_Mode_Iovec16) {
  encode_present_nullable_string<Mode::Iovec16>();
}
TEST(Strings, encode_absent_nullable_string_Mode_Iovec16) {
  encode_absent_nullable_string<Mode::Iovec16>();
}
TEST(Strings, encode_present_nonnullable_bounded_string_Mode_Iovec16) {
  encode_present_nonnullable_bounded_string<Mode::Iovec16>();
}
TEST(Strings, encode_present_nullable_bounded_string_Mode_Iovec16) {
  encode_present_nullable_bounded_string<Mode::Iovec16>();
}
TEST(Strings, encode_absent_nonnullable_bounded_string_error_Mode_Iovec16) {
  encode_absent_nonnullable_bounded_string_error<Mode::Iovec16>();
}
TEST(Strings, encode_absent_nullable_bounded_string_Mode_Iovec16) {
  encode_absent_nullable_bounded_string<Mode::Iovec16>();
}
TEST(Strings, encode_present_nonnullable_bounded_string_short_error_Mode_Iovec16) {
  encode_present_nonnullable_bounded_string_short_error<Mode::Iovec16>();
}
TEST(Strings, encode_present_nullable_bounded_string_short_error_Mode_Iovec16) {
  encode_present_nullable_bounded_string_short_error<Mode::Iovec16>();
}

TEST(Vectors, encode_vector_with_huge_count_Mode_EncodeOnly) {
  encode_vector_with_huge_count<Mode::EncodeOnly>();
}
#if __Fuchsia__
TEST(Vectors, encode_present_nonnullable_vector_of_handles_Mode_EncodeOnly) {
  encode_present_nonnullable_vector_of_handles<Mode::EncodeOnly>();
}
TEST(Vectors, encode_present_nullable_vector_of_handles_Mode_EncodeOnly) {
  encode_present_nullable_vector_of_handles<Mode::EncodeOnly>();
}
TEST(Vectors, encode_absent_nullable_vector_of_handles_Mode_EncodeOnly) {
  encode_absent_nullable_vector_of_handles<Mode::EncodeOnly>();
}
TEST(Vectors, encode_present_nonnullable_bounded_vector_of_handles_Mode_EncodeOnly) {
  encode_present_nonnullable_bounded_vector_of_handles<Mode::EncodeOnly>();
}
TEST(Vectors, encode_present_nullable_bounded_vector_of_handles_Mode_EncodeOnly) {
  encode_present_nullable_bounded_vector_of_handles<Mode::EncodeOnly>();
}
TEST(Vectors, encode_absent_nonnullable_bounded_vector_of_handles_Mode_EncodeOnly) {
  encode_absent_nonnullable_bounded_vector_of_handles<Mode::EncodeOnly>();
}
TEST(Vectors, encode_absent_nullable_bounded_vector_of_handles_Mode_EncodeOnly) {
  encode_absent_nullable_bounded_vector_of_handles<Mode::EncodeOnly>();
}
TEST(Vectors, encode_present_nonnullable_bounded_vector_of_handles_short_error_Mode_EncodeOnly) {
  encode_present_nonnullable_bounded_vector_of_handles_short_error<Mode::EncodeOnly>();
}
TEST(Vectors, encode_present_nullable_bounded_vector_of_handles_short_error_Mode_EncodeOnly) {
  encode_present_nullable_bounded_vector_of_handles_short_error<Mode::EncodeOnly>();
}
#endif

TEST(Vectors, encode_present_nonnullable_vector_of_uint32_Mode_EncodeOnly) {
  encode_present_nonnullable_vector_of_uint32<Mode::EncodeOnly>();
}
TEST(Vectors, encode_present_nullable_vector_of_uint32_Mode_EncodeOnly) {
  encode_present_nullable_vector_of_uint32<Mode::EncodeOnly>();
}
TEST(Vectors, encode_absent_nonnullable_vector_of_uint32_error_Mode_EncodeOnly) {
  encode_absent_nonnullable_vector_of_uint32_error<Mode::EncodeOnly>();
}
TEST(Vectors, encode_absent_nullable_vector_of_uint32_Mode_EncodeOnly) {
  encode_absent_nullable_vector_of_uint32<Mode::EncodeOnly>();
}
TEST(Vectors, encode_absent_nullable_vector_of_uint32_non_zero_length_error_Mode_EncodeOnly) {
  encode_absent_nullable_vector_of_uint32_non_zero_length_error<Mode::EncodeOnly>();
}
TEST(Vectors, encode_present_nonnullable_bounded_vector_of_uint32_Mode_EncodeOnly) {
  encode_present_nonnullable_bounded_vector_of_uint32<Mode::EncodeOnly>();
}
TEST(Vectors, encode_present_nullable_bounded_vector_of_uint32_Mode_EncodeOnly) {
  encode_present_nullable_bounded_vector_of_uint32<Mode::EncodeOnly>();
}
TEST(Vectors, encode_absent_nonnullable_bounded_vector_of_uint32_Mode_EncodeOnly) {
  encode_absent_nonnullable_bounded_vector_of_uint32<Mode::EncodeOnly>();
}
TEST(Vectors, encode_absent_nullable_bounded_vector_of_uint32_Mode_EncodeOnly) {
  encode_absent_nullable_bounded_vector_of_uint32<Mode::EncodeOnly>();
}
TEST(Vectors, encode_present_nonnullable_bounded_vector_of_uint32_short_error_Mode_EncodeOnly) {
  encode_present_nonnullable_bounded_vector_of_uint32_short_error<Mode::EncodeOnly>();
}
TEST(Vectors, encode_present_nullable_bounded_vector_of_uint32_short_error_Mode_EncodeOnly) {
  encode_present_nullable_bounded_vector_of_uint32_short_error<Mode::EncodeOnly>();
}
TEST(Vectors, encode_vector_with_huge_count_Mode_Iovec1FullyLinearized) {
  encode_vector_with_huge_count<Mode::Iovec1FullyLinearized>();
}
TEST(Vectors, encode_vector_with_huge_count_Mode_Iovec16) {
  encode_vector_with_huge_count<Mode::Iovec16>();
}
#if __Fuchsia__
TEST(Vectors, encode_absent_nonnullable_vector_of_handles_error_Mode_EncodeOnly) {
  encode_absent_nonnullable_vector_of_handles_error<Mode::EncodeOnly>();
}
TEST(Vectors, encode_present_nonnullable_vector_of_handles_Mode_Iovec1FullyLinearized) {
  encode_present_nonnullable_vector_of_handles<Mode::Iovec1FullyLinearized>();
}
TEST(Vectors, encode_present_nullable_vector_of_handles_Mode_Iovec1FullyLinearized) {
  encode_present_nullable_vector_of_handles<Mode::Iovec1FullyLinearized>();
}
TEST(Vectors, encode_absent_nullable_vector_of_handles_Mode_Iovec1FullyLinearized) {
  encode_absent_nullable_vector_of_handles<Mode::Iovec1FullyLinearized>();
}
TEST(Vectors, encode_present_nonnullable_bounded_vector_of_handles_Mode_Iovec1FullyLinearized) {
  encode_present_nonnullable_bounded_vector_of_handles<Mode::Iovec1FullyLinearized>();
}
TEST(Vectors, encode_present_nullable_bounded_vector_of_handles_Mode_Iovec1FullyLinearized) {
  encode_present_nullable_bounded_vector_of_handles<Mode::Iovec1FullyLinearized>();
}
TEST(Vectors, encode_absent_nonnullable_bounded_vector_of_handles_Mode_Iovec1FullyLinearized) {
  encode_absent_nonnullable_bounded_vector_of_handles<Mode::Iovec1FullyLinearized>();
}
TEST(Vectors, encode_absent_nullable_bounded_vector_of_handles_Mode_Iovec1FullyLinearized) {
  encode_absent_nullable_bounded_vector_of_handles<Mode::Iovec1FullyLinearized>();
}
TEST(Vectors,
     encode_present_nonnullable_bounded_vector_of_handles_short_error_Mode_Iovec1FullyLinearized) {
  encode_present_nonnullable_bounded_vector_of_handles_short_error<Mode::Iovec1FullyLinearized>();
}
TEST(Vectors,
     encode_present_nullable_bounded_vector_of_handles_short_error_Mode_Iovec1FullyLinearized) {
  encode_present_nullable_bounded_vector_of_handles_short_error<Mode::Iovec1FullyLinearized>();
}
TEST(Vectors, encode_present_nonnullable_vector_of_handles_Mode_Iovec16) {
  encode_present_nonnullable_vector_of_handles<Mode::Iovec16>();
}
TEST(Vectors, encode_present_nullable_vector_of_handles_Mode_Iovec16) {
  encode_present_nullable_vector_of_handles<Mode::Iovec16>();
}
TEST(Vectors, encode_absent_nullable_vector_of_handles_Mode_Iovec16) {
  encode_absent_nullable_vector_of_handles<Mode::Iovec16>();
}
TEST(Vectors, encode_present_nonnullable_bounded_vector_of_handles_Mode_Iovec16) {
  encode_present_nonnullable_bounded_vector_of_handles<Mode::Iovec16>();
}
TEST(Vectors, encode_present_nullable_bounded_vector_of_handles_Mode_Iovec16) {
  encode_present_nullable_bounded_vector_of_handles<Mode::Iovec16>();
}
TEST(Vectors, encode_absent_nonnullable_bounded_vector_of_handles_Mode_Iovec16) {
  encode_absent_nonnullable_bounded_vector_of_handles<Mode::Iovec16>();
}
TEST(Vectors, encode_absent_nullable_bounded_vector_of_handles_Mode_Iovec16) {
  encode_absent_nullable_bounded_vector_of_handles<Mode::Iovec16>();
}
TEST(Vectors, encode_present_nonnullable_bounded_vector_of_handles_short_error_Mode_Iovec16) {
  encode_present_nonnullable_bounded_vector_of_handles_short_error<Mode::Iovec16>();
}
TEST(Vectors, encode_present_nullable_bounded_vector_of_handles_short_error_Mode_Iovec16) {
  encode_present_nullable_bounded_vector_of_handles_short_error<Mode::Iovec16>();
}
#endif
TEST(Vectors, encode_present_nonnullable_vector_of_uint32_Mode_Iovec1FullyLinearized) {
  encode_present_nonnullable_vector_of_uint32<Mode::Iovec1FullyLinearized>();
}
TEST(Vectors, encode_present_nullable_vector_of_uint32_Mode_Iovec1FullyLinearized) {
  encode_present_nullable_vector_of_uint32<Mode::Iovec1FullyLinearized>();
}
TEST(Vectors, encode_absent_nonnullable_vector_of_uint32_error_Mode_Iovec1FullyLinearized) {
  encode_absent_nonnullable_vector_of_uint32_error<Mode::Iovec1FullyLinearized>();
}
TEST(Vectors, encode_absent_nullable_vector_of_uint32_Mode_Iovec1FullyLinearized) {
  encode_absent_nullable_vector_of_uint32<Mode::Iovec1FullyLinearized>();
}
TEST(Vectors,
     encode_absent_nullable_vector_of_uint32_non_zero_length_error_Mode_Iovec1FullyLinearized) {
  encode_absent_nullable_vector_of_uint32_non_zero_length_error<Mode::Iovec1FullyLinearized>();
}
TEST(Vectors, encode_present_nonnullable_bounded_vector_of_uint32_Mode_Iovec1FullyLinearized) {
  encode_present_nonnullable_bounded_vector_of_uint32<Mode::Iovec1FullyLinearized>();
}
TEST(Vectors, encode_present_nullable_bounded_vector_of_uint32_Mode_Iovec1FullyLinearized) {
  encode_present_nullable_bounded_vector_of_uint32<Mode::Iovec1FullyLinearized>();
}
TEST(Vectors, encode_absent_nonnullable_bounded_vector_of_uint32_Mode_Iovec1FullyLinearized) {
  encode_absent_nonnullable_bounded_vector_of_uint32<Mode::Iovec1FullyLinearized>();
}
TEST(Vectors, encode_absent_nullable_bounded_vector_of_uint32_Mode_Iovec1FullyLinearized) {
  encode_absent_nullable_bounded_vector_of_uint32<Mode::Iovec1FullyLinearized>();
}
TEST(Vectors,
     encode_present_nonnullable_bounded_vector_of_uint32_short_error_Mode_Iovec1FullyLinearized) {
  encode_present_nonnullable_bounded_vector_of_uint32_short_error<Mode::Iovec1FullyLinearized>();
}
TEST(Vectors,
     encode_present_nullable_bounded_vector_of_uint32_short_error_Mode_Iovec1FullyLinearized) {
  encode_present_nullable_bounded_vector_of_uint32_short_error<Mode::Iovec1FullyLinearized>();
}
TEST(Vectors, encode_present_nonnullable_vector_of_uint32_Mode_Iovec16) {
  encode_present_nonnullable_vector_of_uint32<Mode::Iovec16>();
}
TEST(Vectors, encode_present_nullable_vector_of_uint32_Mode_Iovec16) {
  encode_present_nullable_vector_of_uint32<Mode::Iovec16>();
}
TEST(Vectors, encode_absent_nonnullable_vector_of_uint32_error_Mode_Iovec16) {
  encode_absent_nonnullable_vector_of_uint32_error<Mode::Iovec16>();
}
TEST(Vectors, encode_absent_nullable_vector_of_uint32_Mode_Iovec16) {
  encode_absent_nullable_vector_of_uint32<Mode::Iovec16>();
}
TEST(Vectors, encode_absent_nullable_vector_of_uint32_non_zero_length_error_Mode_Iovec16) {
  encode_absent_nullable_vector_of_uint32_non_zero_length_error<Mode::Iovec16>();
}
TEST(Vectors, encode_present_nonnullable_bounded_vector_of_uint32_Mode_Iovec16) {
  encode_present_nonnullable_bounded_vector_of_uint32<Mode::Iovec16>();
}
TEST(Vectors, encode_present_nullable_bounded_vector_of_uint32_Mode_Iovec16) {
  encode_present_nullable_bounded_vector_of_uint32<Mode::Iovec16>();
}
TEST(Vectors, encode_absent_nonnullable_bounded_vector_of_uint32_Mode_Iovec16) {
  encode_absent_nonnullable_bounded_vector_of_uint32<Mode::Iovec16>();
}
TEST(Vectors, encode_absent_nullable_bounded_vector_of_uint32_Mode_Iovec16) {
  encode_absent_nullable_bounded_vector_of_uint32<Mode::Iovec16>();
}
TEST(Vectors, encode_present_nonnullable_bounded_vector_of_uint32_short_error_Mode_Iovec16) {
  encode_present_nonnullable_bounded_vector_of_uint32_short_error<Mode::Iovec16>();
}
TEST(Vectors, encode_present_nullable_bounded_vector_of_uint32_short_error_Mode_Iovec16) {
  encode_present_nullable_bounded_vector_of_uint32_short_error<Mode::Iovec16>();
}

#if __Fuchsia__
TEST(Vectors, encode_absent_nonnullable_vector_of_handles_error_Mode_Iovec1FullyLinearized) {
  encode_absent_nonnullable_vector_of_handles_error<Mode::Iovec1FullyLinearized>();
}
TEST(Vectors, encode_absent_nonnullable_vector_of_handles_error_Mode_Iovec16) {
  encode_absent_nonnullable_vector_of_handles_error<Mode::Iovec16>();
}
TEST(Structs, encode_nested_nonnullable_structs_Mode_EncodeOnly) {
  encode_nested_nonnullable_structs<Mode::EncodeOnly>();
}
TEST(Structs, encode_nested_nonnullable_structs_zero_padding_Mode_EncodeOnly) {
  encode_nested_nonnullable_structs_zero_padding<Mode::EncodeOnly>();
}
TEST(Structs, encode_nested_nullable_structs_Mode_EncodeOnly) {
  encode_nested_nullable_structs<Mode::EncodeOnly>();
}
TEST(Structs, encode_nested_nonnullable_structs_Mode_Iovec1FullyLinearized) {
  encode_nested_nonnullable_structs<Mode::Iovec1FullyLinearized>();
}
TEST(Structs, encode_nested_nonnullable_structs_zero_padding_Mode_Iovec1FullyLinearized) {
  encode_nested_nonnullable_structs_zero_padding<Mode::Iovec1FullyLinearized>();
}
TEST(Structs, encode_nested_nullable_structs_Mode_Iovec1FullyLinearized) {
  encode_nested_nullable_structs<Mode::Iovec1FullyLinearized>();
}
TEST(Structs, encode_nested_nonnullable_structs_Mode_Iovec16) {
  encode_nested_nonnullable_structs<Mode::Iovec16>();
}
TEST(Structs, encode_nested_nonnullable_structs_zero_padding_Mode_Iovec16) {
  encode_nested_nonnullable_structs_zero_padding<Mode::Iovec16>();
}
TEST(Structs, encode_nested_nullable_structs_Mode_Iovec16) {
  encode_nested_nullable_structs<Mode::Iovec16>();
}
#endif

TEST(Msg, EncodeOutgoingByteMsg) {
  struct {
    FIDL_ALIGNDECL
    BoolStruct b{true};
  } obj = {};
  const char* error = nullptr;
  fidl_outgoing_msg_byte_t msg = {
      .bytes = &obj,
      .handles = nullptr,
      .num_bytes = FIDL_ALIGN(sizeof(obj)),
      .num_handles = 0,
  };
  uint32_t actual_handles = 42;
  zx_status_t status =
      fidl_encode_msg(&fidl_test_coding_BoolStructTable, &msg, &actual_handles, &error);
  EXPECT_EQ(actual_handles, 0);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
}

TEST(Iovec, SimpleObject) {
  BoolStruct obj{true};
  auto iovecs = std::make_unique<zx_channel_iovec_t[]>(ZX_CHANNEL_MAX_MSG_IOVECS);
  auto buffer = std::make_unique<uint8_t[]>(ZX_CHANNEL_MAX_MSG_BYTES);
  uint32_t out_actual_iovecs;
  uint32_t out_actual_handles;
  const char* out_error = nullptr;
  zx_status_t status = fidl::internal::EncodeIovecEtc<FIDL_WIRE_FORMAT_VERSION_V2>(
      &fidl_test_coding_BoolStructTable, &obj, iovecs.get(), ZX_CHANNEL_MAX_MSG_IOVECS, nullptr, 0,
      buffer.get(), ZX_CHANNEL_MAX_MSG_BYTES, &out_actual_iovecs, &out_actual_handles, &out_error);
  ASSERT_EQ(status, ZX_OK);
  EXPECT_NULL(out_error);
  ASSERT_EQ(out_actual_iovecs, 1);
  EXPECT_EQ(out_actual_handles, 0);
  EXPECT_EQ(iovecs[0].buffer, buffer.get());
  EXPECT_EQ(iovecs[0].capacity, 8);
  EXPECT_EQ(iovecs[0].reserved, 0);
  EXPECT_EQ(*reinterpret_cast<const bool*>(iovecs[0].buffer), obj.v);
}

TEST(Iovec, EncodeDoesntMutateVectorObject) {
  std::vector<uint32_t> arr = {1u, 2u, 3u};
  Uint32VectorStruct obj{
      .vec = fidl::VectorView<uint32_t>::FromExternal(arr),
  };

  constexpr size_t obj_size = sizeof(Uint32VectorStruct);
  size_t vec_body_size = sizeof(uint32_t) * obj.vec.count();

  auto make_snapshot = [&](Uint32VectorStruct* obj) {
    std::vector<uint8_t> snapshot;
    snapshot.resize(obj_size);
    memcpy(snapshot.data(), obj, obj_size);
    snapshot.resize(obj_size + vec_body_size);
    memcpy(snapshot.data() + obj_size, obj->vec.data(), vec_body_size);
    return snapshot;
  };
  auto initial_snapshot = make_snapshot(&obj);

  auto iovecs = std::make_unique<zx_channel_iovec_t[]>(ZX_CHANNEL_MAX_MSG_IOVECS);
  auto buffer = std::make_unique<uint8_t[]>(ZX_CHANNEL_MAX_MSG_BYTES);
  uint32_t out_actual_iovecs;
  uint32_t out_actual_handles;
  const char* out_error = nullptr;
  zx_status_t status = fidl::internal::EncodeIovecEtc<FIDL_WIRE_FORMAT_VERSION_V2>(
      &fidl_test_coding_Uint32VectorStructTable, &obj, iovecs.get(), ZX_CHANNEL_MAX_MSG_IOVECS,
      nullptr, 0, buffer.get(), ZX_CHANNEL_MAX_MSG_BYTES, &out_actual_iovecs, &out_actual_handles,
      &out_error);
  ASSERT_EQ(status, ZX_OK);
  EXPECT_NULL(out_error);
  ASSERT_EQ(out_actual_iovecs, 3);
  EXPECT_EQ(out_actual_handles, 0);
  EXPECT_EQ(iovecs[0].buffer, buffer.get());
  EXPECT_EQ(iovecs[0].capacity, obj_size);
  EXPECT_EQ(iovecs[0].reserved, 0);
  EXPECT_EQ(iovecs[1].buffer, arr.data());
  EXPECT_EQ(iovecs[1].capacity, vec_body_size);
  EXPECT_EQ(iovecs[1].reserved, 0);
  EXPECT_EQ(iovecs[2].buffer, buffer.get() + obj_size + 4);
  EXPECT_EQ(iovecs[2].capacity, 4);
  EXPECT_EQ(iovecs[2].reserved, 0);

  auto final_snapshot = make_snapshot(&obj);
  EXPECT_EQ(initial_snapshot.size(), final_snapshot.size());
  EXPECT_BYTES_EQ(initial_snapshot.data(), final_snapshot.data(), initial_snapshot.size());
}

TEST(Iovec, ExceedVectorBufferCount) {
  std::vector<uint32_t> arr = {1u, 2u, 3u};
  Uint32VectorStruct obj{
      .vec = fidl::VectorView<uint32_t>::FromExternal(arr),
  };

  // 3 iovecs are needed to directly point at the vector body.
  // When 1 or 2 are present, the encoder should linearize into just the first
  // iovec.
  auto iovecs = std::make_unique<zx_channel_iovec_t[]>(2);
  auto buffer = std::make_unique<uint8_t[]>(ZX_CHANNEL_MAX_MSG_BYTES);
  uint32_t out_actual_iovecs;
  uint32_t out_actual_handles;
  const char* out_error = nullptr;
  zx_status_t status = fidl::internal::EncodeIovecEtc<FIDL_WIRE_FORMAT_VERSION_V2>(
      &fidl_test_coding_Uint32VectorStructTable, &obj, iovecs.get(), 2, nullptr, 0, buffer.get(),
      ZX_CHANNEL_MAX_MSG_BYTES, &out_actual_iovecs, &out_actual_handles, &out_error);
  ASSERT_EQ(status, ZX_OK);
  EXPECT_NULL(out_error);
  ASSERT_EQ(out_actual_iovecs, 1);
  EXPECT_EQ(out_actual_handles, 0);
  EXPECT_EQ(iovecs[0].buffer, buffer.get());
  EXPECT_EQ(iovecs[0].capacity, sizeof(obj) + arr.size() * sizeof(uint32_t) + 4);
  EXPECT_EQ(iovecs[0].reserved, 0);
  EXPECT_BYTES_EQ(iovecs[0].buffer, &obj, 8);
  EXPECT_BYTES_EQ(obj.vec.data(), arr.data(), arr.size() * sizeof(uint32_t));
}

TEST(Iovec, MatchNeededVectorBufferCount) {
  std::vector<uint32_t> arr = {1u, 2u, 3u};
  Uint32VectorStruct obj{
      .vec = fidl::VectorView<uint32_t>::FromExternal(arr),
  };

  // With 3 iovecs, the second iovec will directly point at the vector body.
  auto iovecs = std::make_unique<zx_channel_iovec_t[]>(3);
  auto buffer = std::make_unique<uint8_t[]>(ZX_CHANNEL_MAX_MSG_BYTES);
  uint32_t out_actual_iovecs;
  uint32_t out_actual_handles;
  const char* out_error = nullptr;
  zx_status_t status = fidl::internal::EncodeIovecEtc<FIDL_WIRE_FORMAT_VERSION_V2>(
      &fidl_test_coding_Uint32VectorStructTable, &obj, iovecs.get(), 3, nullptr, 0, buffer.get(),
      ZX_CHANNEL_MAX_MSG_BYTES, &out_actual_iovecs, &out_actual_handles, &out_error);
  ASSERT_EQ(status, ZX_OK);
  EXPECT_NULL(out_error);
  ASSERT_EQ(out_actual_iovecs, 3);
  EXPECT_EQ(out_actual_handles, 0);
}

TEST(Iovec, TooFewBytes) {
  fidl::Arena allocator;
  const char input[] = "abcd";
  StringStruct str = {.str = fidl::StringView(allocator, input)};

  // This is only enough to hold the primary object, and not enough to hold
  // the out-of-line string.
  constexpr uint32_t kBufferSize = FIDL_ALIGNMENT;
  FIDL_ALIGNDECL uint8_t buffer[kBufferSize];
  constexpr uint32_t kIovecSize = 2;
  zx_channel_iovec_t out_iovec[kIovecSize];
  uint32_t out_actual_iovecs;
  uint32_t out_actual_handles;
  const char* out_error = nullptr;
  zx_status_t status = fidl::internal::EncodeIovecEtc<FIDL_WIRE_FORMAT_VERSION_V2>(
      &fidl_test_coding_StringStructTable, &str, out_iovec, kIovecSize, nullptr, 0, buffer,
      kBufferSize, &out_actual_iovecs, &out_actual_handles, &out_error);
  ASSERT_EQ(status, ZX_ERR_BUFFER_TOO_SMALL);
  EXPECT_NOT_NULL(out_error);
}

#ifdef __Fuchsia__
TEST(Iovec, IovecEtcWithHandles) {
  array_of_nonnullable_handles_message_layout message = {};
  message.inline_struct.handles[0] = dummy_handle_0;
  message.inline_struct.handles[1] = dummy_handle_1;
  message.inline_struct.handles[2] = dummy_handle_2;
  message.inline_struct.handles[3] = dummy_handle_3;
  constexpr uint32_t kIovecSize = 2;
  constexpr uint32_t kHandleSize = 4;
  zx_channel_iovec_t out_iovec[kIovecSize];
  zx_handle_disposition_t handles[kHandleSize];
  uint8_t buffer[sizeof(message)];
  uint32_t out_actual_iovecs;
  uint32_t out_actual_handles;
  const char* out_error = nullptr;
  zx_status_t status = fidl::internal::EncodeIovecEtc<FIDL_WIRE_FORMAT_VERSION_V2>(
      &array_of_nonnullable_handles_message_type, &message, out_iovec, kIovecSize, handles,
      kHandleSize, buffer, sizeof(buffer), &out_actual_iovecs, &out_actual_handles, &out_error);
  ASSERT_EQ(status, ZX_OK);
  EXPECT_EQ(out_error, nullptr);
  EXPECT_NULL(out_error);
  EXPECT_EQ(kHandleSize, out_actual_handles);
  FidlHandleDispositionCloseMany(handles, out_actual_handles);
}

TEST(Iovec, TooFewHandles) {
  array_of_nonnullable_handles_message_layout message = {};
  message.inline_struct.handles[0] = dummy_handle_0;
  message.inline_struct.handles[1] = dummy_handle_1;
  message.inline_struct.handles[2] = dummy_handle_2;
  message.inline_struct.handles[3] = dummy_handle_3;
  constexpr uint32_t kIovecSize = 2;
  constexpr uint32_t kHandleSize = 1;
  zx_channel_iovec_t iovec[2];
  zx_handle_disposition_t handles[1];
  uint8_t buffer[sizeof(message)];
  uint32_t out_actual_iovecs;
  uint32_t out_actual_handles;
  const char* out_error = nullptr;
  zx_status_t status = fidl::internal::EncodeIovecEtc<FIDL_WIRE_FORMAT_VERSION_V2>(
      &array_of_nonnullable_handles_message_type, &message, iovec, kIovecSize, handles, kHandleSize,
      buffer, sizeof(buffer), &out_actual_iovecs, &out_actual_handles, &out_error);
  ASSERT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NOT_NULL(out_error);
}
#endif

}  // namespace
}  // namespace fidl
