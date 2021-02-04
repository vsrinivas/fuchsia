// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/message.h>

#include <gtest/gtest.h>

TEST(OutgoingMessageAdaptorFromC, Byte) {
  uint8_t bytes[1];
  zx_handle_disposition_t handles[2];
  fidl_outgoing_msg_t orig_msg = {
      .type = FIDL_OUTGOING_MSG_TYPE_BYTE,
      .byte =
          {
              .bytes = bytes,
              .handles = handles,
              .num_bytes = 1,
              .num_handles = 2,
          },
  };
  fidl::OutgoingMessageAdaptorFromC any_outgoing_msg(&orig_msg);
  fidl_outgoing_msg_t& new_msg = *any_outgoing_msg.GetOutgoingMessage().message();
  EXPECT_EQ(orig_msg.type, new_msg.type);
  EXPECT_EQ(orig_msg.byte.bytes, new_msg.byte.bytes);
  EXPECT_EQ(orig_msg.byte.handles, new_msg.byte.handles);
  EXPECT_EQ(orig_msg.byte.num_bytes, new_msg.byte.num_bytes);
  EXPECT_EQ(orig_msg.byte.num_handles, new_msg.byte.num_handles);
}

TEST(OutgoingMessageAdaptorFromC, Iovec) {
  zx_channel_iovec_t iovecs[1];
  zx_handle_disposition_t handles[2];
  fidl_outgoing_msg_t orig_msg = {
      .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
      .iovec =
          {
              .iovecs = iovecs,
              .num_iovecs = 1,
              .handles = handles,
              .num_handles = 2,
          },
  };
  fidl::OutgoingMessageAdaptorFromC any_outgoing_msg(&orig_msg);
  fidl_outgoing_msg_t& new_msg = *any_outgoing_msg.GetOutgoingMessage().message();
  EXPECT_EQ(orig_msg.type, new_msg.type);
  EXPECT_EQ(orig_msg.iovec.iovecs, new_msg.iovec.iovecs);
  EXPECT_EQ(orig_msg.iovec.handles, new_msg.iovec.handles);
  EXPECT_EQ(orig_msg.iovec.num_iovecs, new_msg.iovec.num_iovecs);
  EXPECT_EQ(orig_msg.iovec.num_handles, new_msg.iovec.num_handles);
}
