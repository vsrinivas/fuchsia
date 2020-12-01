// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/message.h>
#include <lib/zx/event.h>

#include <gtest/gtest.h>

TEST(OutgoingToIncomingMessage, TooManyHandles) {
  fidl_outgoing_msg_t input = {
      .bytes = nullptr,
      .handles = nullptr,
      .num_bytes = 0,
      .num_handles = 2,
  };
  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, fidl::OutgoingToIncomingMessage(&input, nullptr, 1, nullptr));
}

#ifdef __Fuchsia__
TEST(OutgoingToIncomingMessage, Fuchsia) {
  char bytes[16];
  zx::event ev;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
  zx_handle_disposition_t hd[1] = {zx_handle_disposition_t{
      .operation = ZX_HANDLE_OP_MOVE,
      .handle = ev.get(),
      .type = ZX_OBJ_TYPE_NONE,
      .rights = ZX_RIGHT_SAME_RIGHTS,
      .result = ZX_OK,
  }};
  fidl_outgoing_msg_t input = {
      .bytes = bytes,
      .handles = hd,
      .num_bytes = 16,
      .num_handles = 1,
  };
  zx_handle_info_t hi[1];
  fidl_incoming_msg_t output;
  ASSERT_EQ(ZX_OK, fidl::OutgoingToIncomingMessage(&input, hi, 1, &output));
  EXPECT_EQ(output.bytes, bytes);
  EXPECT_EQ(output.num_bytes, 16u);
  EXPECT_EQ(output.num_handles, 1u);
  EXPECT_EQ(output.handles[0].handle, ev.get());
  EXPECT_EQ(output.handles[0].type, ZX_OBJ_TYPE_EVENT);
  EXPECT_EQ(output.handles[0].rights, ZX_DEFAULT_EVENT_RIGHTS);
}
#endif
