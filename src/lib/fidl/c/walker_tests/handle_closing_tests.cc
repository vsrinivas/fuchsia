// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>
#include <lib/fidl/internal.h>
#include <lib/zx/channel.h>
#include <limits.h>
#include <stddef.h>
#include <zircon/syscalls.h>

#include <new>

#include <zxtest/zxtest.h>

#include "array_util.h"
#include "fidl_coded_types.h"
#include "fidl_structs.h"

// Tests that fidl_close_handles correctly closes all handles in a message,
// even if some of them were moved out/malformed/invalid.

namespace fidl {
namespace {

void helper_expect_peer_valid(zx_handle_t channel) {
  const char* foo = "hello";
  EXPECT_EQ(zx_channel_write(channel, 0, foo, 5, nullptr, 0), ZX_OK);
}

void helper_expect_peer_invalid(zx_handle_t channel) {
  const char* foo = "hello";
  EXPECT_EQ(zx_channel_write(channel, 0, foo, 5, nullptr, 0), ZX_ERR_PEER_CLOSED);
}

TEST(Handles, close_single_present_handle) {
  zx_handle_t* channel_0 = new zx_handle_t;
  // Capture the extra handle here; it will not be cleaned by fidl_close_handles
  zx::channel channel_1 = {};

  // Unsafely open a channel, which should be closed automatically by fidl_close_handles
  {
    zx_handle_t out0, out1;
    EXPECT_EQ(zx_channel_create(0, &out0, &out1), ZX_OK);
    *channel_0 = out0;
    channel_1 = zx::channel(out1);
  }

  nonnullable_handle_message_layout message = {};
  message.inline_struct.handle = *channel_0;

  helper_expect_peer_valid(channel_1.get());

  const char* error = nullptr;
  auto status = fidl_close_handles(&nonnullable_handle_message_type, &message, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);
  helper_expect_peer_invalid(channel_1.get());
  EXPECT_EQ(message.inline_struct.handle, ZX_HANDLE_INVALID);

  delete channel_0;
}

TEST(Handles, close_multiple_present_handles_with_some_invalid) {
  zx_handle_t* channels_0 = new zx_handle_t[3];
  // Capture the extra handles here; these will not be cleaned by fidl_close_handles
  zx::channel channels_1[3] = {};

  // Unsafely open a few channels, which should be closed automatically by fidl_close_handles
  for (int i = 0; i < 3; i++) {
    zx_handle_t out0, out1;
    EXPECT_EQ(zx_channel_create(0, &out0, &out1), ZX_OK);
    channels_0[i] = out0;
    channels_1[i] = zx::channel(out1);
  }

  helper_expect_peer_valid(channels_1[0].get());
  helper_expect_peer_valid(channels_1[1].get());
  helper_expect_peer_valid(channels_1[2].get());

  // Make the second handle invalid
  multiple_nonnullable_handles_message_layout message = {};
  message.inline_struct.handle_0 = channels_0[0];
  message.inline_struct.handle_1 = ZX_HANDLE_INVALID;
  message.inline_struct.handle_2 = channels_0[2];

  const char* error = nullptr;
  auto status = fidl_close_handles(&multiple_nonnullable_handles_message_type, &message, &error);

  // Since the message is invalid, fidl_close_handles will error, but all the handles
  // in the message must still be closed despite the error.
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  const char expected_error_msg[] = "message is missing a non-nullable handle";
  EXPECT_STR_EQ(expected_error_msg, error, "wrong error msg");

  // Second channel should remain valid, since it was inaccessible to fidl_close_handles
  helper_expect_peer_invalid(channels_1[0].get());
  helper_expect_peer_valid(channels_1[1].get());
  helper_expect_peer_invalid(channels_1[2].get());

  // Handles 0, 2 have been closed; it is now an error to re-close them.
  EXPECT_EQ(zx_handle_close(channels_0[1]), ZX_OK);

  EXPECT_EQ(message.inline_struct.data_0, 0u);
  EXPECT_EQ(message.inline_struct.data_1, 0u);
  EXPECT_EQ(message.inline_struct.data_2, 0u);
  // Handles in the message struct are released.
  EXPECT_EQ(message.inline_struct.handle_0, ZX_HANDLE_INVALID);
  EXPECT_EQ(message.inline_struct.handle_1, ZX_HANDLE_INVALID);
  EXPECT_EQ(message.inline_struct.handle_2, ZX_HANDLE_INVALID);

  delete[] channels_0;
}

TEST(Arrays, close_array_of_present_handles) {
  zx_handle_t* channels_0 = new zx_handle_t[4];
  // Capture the extra handles here; these will not be cleaned by fidl_close_handles
  zx::channel channels_1[4] = {};

  // Unsafely open a few channels, which should be closed automatically by fidl_close_handles
  for (int i = 0; i < 4; i++) {
    zx_handle_t out0, out1;
    EXPECT_EQ(zx_channel_create(0, &out0, &out1), ZX_OK);
    channels_0[i] = out0;
    channels_1[i] = zx::channel(out1);
  }

  array_of_nonnullable_handles_message_layout message = {};
  for (int i = 0; i < 4; i++) {
    message.inline_struct.handles[i] = channels_0[i];
  }

  helper_expect_peer_valid(channels_1[0].get());
  helper_expect_peer_valid(channels_1[1].get());
  helper_expect_peer_valid(channels_1[2].get());
  helper_expect_peer_valid(channels_1[3].get());

  const char* error = nullptr;
  auto status = fidl_close_handles(&array_of_nonnullable_handles_message_type, &message, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);

  helper_expect_peer_invalid(channels_1[0].get());
  helper_expect_peer_invalid(channels_1[1].get());
  helper_expect_peer_invalid(channels_1[2].get());
  helper_expect_peer_invalid(channels_1[3].get());

  // Handles in the message struct are released.
  EXPECT_EQ(message.inline_struct.handles[0], ZX_HANDLE_INVALID);
  EXPECT_EQ(message.inline_struct.handles[1], ZX_HANDLE_INVALID);
  EXPECT_EQ(message.inline_struct.handles[2], ZX_HANDLE_INVALID);
  EXPECT_EQ(message.inline_struct.handles[3], ZX_HANDLE_INVALID);

  delete[] channels_0;
}

TEST(Arrays, close_out_of_line_array_of_nonnullable_handles) {
  zx_handle_t* channels_0 = new zx_handle_t[4];
  // Capture the extra handles here; these will not be cleaned by fidl_close_handles
  zx::channel channels_1[4] = {};

  // Unsafely open a few channels, which should be closed automatically by fidl_close_handles
  for (int i = 0; i < 4; i++) {
    zx_handle_t out0, out1;
    EXPECT_EQ(zx_channel_create(0, &out0, &out1), ZX_OK);
    channels_0[i] = out0;
    channels_1[i] = zx::channel(out1);
  }

  out_of_line_array_of_nonnullable_handles_message_layout message = {};
  message.inline_struct.maybe_array = &message.data;
  for (int i = 0; i < 4; i++) {
    message.data.handles[i] = channels_0[i];
  }

  helper_expect_peer_valid(channels_1[0].get());
  helper_expect_peer_valid(channels_1[1].get());
  helper_expect_peer_valid(channels_1[2].get());
  helper_expect_peer_valid(channels_1[3].get());

  const char* error = nullptr;
  auto status =
      fidl_close_handles(&out_of_line_array_of_nonnullable_handles_message_type, &message, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);

  helper_expect_peer_invalid(channels_1[0].get());
  helper_expect_peer_invalid(channels_1[1].get());
  helper_expect_peer_invalid(channels_1[2].get());
  helper_expect_peer_invalid(channels_1[3].get());

  // Handles in the message struct are released.
  EXPECT_EQ(message.data.handles[0], ZX_HANDLE_INVALID);
  EXPECT_EQ(message.data.handles[1], ZX_HANDLE_INVALID);
  EXPECT_EQ(message.data.handles[2], ZX_HANDLE_INVALID);
  EXPECT_EQ(message.data.handles[3], ZX_HANDLE_INVALID);

  delete[] channels_0;
}

// This number of handles is guaranteed to not fit in a channel call.
// Nonetheless, they must be closed by fidl_close_handles.
constexpr int kTooBigNumHandles = ZX_CHANNEL_MAX_MSG_HANDLES * 2;

struct unbounded_too_large_nullable_vector_of_handles_inline_data {
  alignas(FIDL_ALIGNMENT) fidl_message_header_t header;
  fidl_vector_t vector;
};
struct unbounded_too_large_nullable_vector_of_handles_message_layout {
  alignas(FIDL_ALIGNMENT) unbounded_too_large_nullable_vector_of_handles_inline_data inline_struct;
  alignas(FIDL_ALIGNMENT) zx_handle_t handles[kTooBigNumHandles];
};
const FidlCodedVector unbounded_too_large_nullable_vector_of_handles = {
    .tag = kFidlTypeVector,
    .nullable = kFidlNullability_Nullable,
    .max_count = FIDL_MAX_SIZE,
    .element_size = sizeof(zx_handle_t),
    .element = &nullable_handle,
};
static const FidlStructElement unbounded_too_large_nullable_vector_of_handles_fields[] = {
    FidlStructElement::Field(&unbounded_too_large_nullable_vector_of_handles,
                             offsetof(unbounded_too_large_nullable_vector_of_handles_message_layout,
                                      inline_struct.vector),
                             true),
};
const FidlCodedStruct unbounded_too_large_nullable_vector_of_handles_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = 1,
    .size = sizeof(unbounded_too_large_nullable_vector_of_handles_inline_data),
    .elements = unbounded_too_large_nullable_vector_of_handles_fields,
    .name = "unbounded_too_large_nullable_vector_of_handles_message",
};

TEST(Vectors, close_present_too_large_nullable_vector_of_handles) {
  zx_handle_t* channels_0 = new zx_handle_t[kTooBigNumHandles];
  // Capture the extra handles here; these will not be cleaned by fidl_close_handles
  zx::channel channels_1[kTooBigNumHandles] = {};

  // Unsafely open a few channels, which should be closed automatically by fidl_close_handles
  for (int i = 0; i < kTooBigNumHandles; i++) {
    zx_handle_t out0, out1;
    EXPECT_EQ(zx_channel_create(0, &out0, &out1), ZX_OK);
    channels_0[i] = out0;
    channels_1[i] = zx::channel(out1);
  }

  unbounded_too_large_nullable_vector_of_handles_message_layout message = {};
  message.inline_struct.vector = fidl_vector_t{kTooBigNumHandles, &message.handles[0]};
  for (int i = 0; i < kTooBigNumHandles; i++) {
    message.handles[i] = channels_0[i];
    helper_expect_peer_valid(channels_1[i].get());
  }

  const char* error = nullptr;
  auto status = fidl_close_handles(&unbounded_too_large_nullable_vector_of_handles_message_type,
                                   &message, &error);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, "%s", error);

  for (int i = 0; i < kTooBigNumHandles; i++) {
    helper_expect_peer_invalid(channels_1[i].get());
  }

  auto message_handles = reinterpret_cast<zx_handle_t*>(message.inline_struct.vector.data);
  for (int i = 0; i < kTooBigNumHandles; i++) {
    EXPECT_EQ(message_handles[i], ZX_HANDLE_INVALID);
  }

  delete[] channels_0;
}

}  // namespace
}  // namespace fidl
