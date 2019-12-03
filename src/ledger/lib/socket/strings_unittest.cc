// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/socket/strings.h"

#include <lib/zx/socket.h>

#include <string>

#include "gtest/gtest.h"

namespace ledger {
namespace {

TEST(SocketAndString, BlockingCopy) {
  zx::socket socket0, socket1;
  EXPECT_EQ(ZX_OK, zx::socket::create(0, &socket0, &socket1));

  EXPECT_TRUE(BlockingCopyFromString("Payload", socket0));
  socket0.reset();

  std::string result;
  EXPECT_TRUE(BlockingCopyToString(std::move(socket1), &result));
  EXPECT_EQ(result, "Payload");

  zx::socket socket = WriteStringToSocket("Another payload");

  EXPECT_TRUE(BlockingCopyToString(std::move(socket), &result));
  EXPECT_EQ(result, "Another payload");
}

}  // namespace
}  // namespace ledger
