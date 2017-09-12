// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mx/socket.h>

#include <string>

#include "gtest/gtest.h"
#include "lib/fsl/socket/strings.h"

namespace fsl {
namespace {

TEST(SocketAndString, BlockingCopy) {
  mx::socket socket0, socket1;
  EXPECT_EQ(MX_OK, mx::socket::create(0, &socket0, &socket1));

  EXPECT_TRUE(BlockingCopyFromString("Payload", socket0));
  socket0.reset();

  std::string result;
  EXPECT_TRUE(BlockingCopyToString(std::move(socket1), &result));
  EXPECT_EQ(result, "Payload");

  mx::socket socket = WriteStringToSocket("Another payload");

  EXPECT_TRUE(BlockingCopyToString(std::move(socket), &result));
  EXPECT_EQ(result, "Another payload");
}

}  // namespace
}  // namespace fsl
