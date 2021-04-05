// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/message.h>

#include <gtest/gtest.h>

TEST(OutgoingMessage, OutgoingMessageByteCountTest) {
  uint8_t bytes[2] = {};
  fidl::OutgoingMessage msgSuccess(bytes, 1, 1, nullptr, 0, 0);
  EXPECT_EQ(ZX_OK, msgSuccess.status());
  fidl::OutgoingMessage msgFail(bytes, 1, 2, nullptr, 0, 0);
  EXPECT_EQ(ZX_ERR_BUFFER_TOO_SMALL, msgFail.status());
}

TEST(OutgoingMessage, OutgoingMessageHandleCountTest) {
  zx_handle_disposition_t handles[2] = {};
  fidl::OutgoingMessage msgSuccess(nullptr, 0, 0, handles, 1, 1);
  EXPECT_EQ(ZX_OK, msgSuccess.status());
  fidl::OutgoingMessage msgFail(nullptr, 0, 0, handles, 1, 2);
  EXPECT_EQ(ZX_ERR_BUFFER_TOO_SMALL, msgFail.status());
}

TEST(OutgoingMessage, OutgoingMessageBytesMatchTest) {
  uint8_t bytes1[]{1, 2, 3, 4};
  fidl::OutgoingMessage msg1(bytes1, sizeof(bytes1), sizeof(bytes1), nullptr, 0, 0);

  // Bytes should match even if one has handles and the other doesn't.
  uint8_t bytes2[]{1, 2, 3, 4};
  zx_handle_disposition_t hd;
  ASSERT_EQ(ZX_OK, zx_event_create(0, &hd.handle));
  fidl::OutgoingMessage msg2(bytes2, sizeof(bytes2), sizeof(bytes2), &hd, 1, 1);

  EXPECT_TRUE(msg1.BytesMatch(msg2));
  EXPECT_TRUE(msg2.BytesMatch(msg1));
}

TEST(OutgoingMessage, OutgoingMessageBytesMismatchTest) {
  uint8_t bytes1[]{1, 2, 3, 4};
  fidl::OutgoingMessage msg1(bytes1, sizeof(bytes1), sizeof(bytes1), nullptr, 0, 0);

  uint8_t bytes2[]{1, 2, 3, 5};
  fidl::OutgoingMessage msg2(bytes2, sizeof(bytes2), sizeof(bytes2), nullptr, 0, 0);

  uint8_t bytes3[]{1, 2, 3};
  fidl::OutgoingMessage msg3(bytes3, sizeof(bytes3), sizeof(bytes3), nullptr, 0, 0);

  EXPECT_FALSE(msg1.BytesMatch(msg2));
  EXPECT_FALSE(msg1.BytesMatch(msg3));
  EXPECT_FALSE(msg2.BytesMatch(msg1));
  EXPECT_FALSE(msg2.BytesMatch(msg3));
  EXPECT_FALSE(msg3.BytesMatch(msg1));
  EXPECT_FALSE(msg3.BytesMatch(msg2));
}

TEST(OutgoingMessage, OutgoingMessageByteCopySizeTest) {
  uint8_t bytes[]{1, 2, 3, 4};
  fidl::OutgoingMessage msg(bytes, sizeof(bytes), sizeof(bytes), nullptr, 0, 0);
  EXPECT_EQ(4u, msg.CopyBytes().size());
}

TEST(OutgoingMessage, OutgoingMessageBytes) {
  uint8_t bytes[]{1, 2, 3, 4};
  fidl::OutgoingMessage msg(bytes, sizeof(bytes), sizeof(bytes), nullptr, 0, 0);
  fidl::OutgoingMessage::CopiedBytes msg_bytes = msg.CopyBytes();
  EXPECT_EQ(sizeof(bytes), msg_bytes.size());
  EXPECT_NE(bytes, msg_bytes.data());
  EXPECT_EQ(0, memcmp(bytes, msg_bytes.data(), sizeof(bytes)));
}
