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
