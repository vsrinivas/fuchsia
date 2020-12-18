// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/llcpp/client_end.h>
#include <lib/zx/object.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls/object.h>

#include <gtest/gtest.h>
#include <llcpptest/protocol/test/llcpp/fidl.h>

namespace llcpp_test = ::llcpp::llcpptest::protocol::test;

TEST(ClientEnd, Trivial) {
  fidl::ClientEnd<llcpp_test::Frobinator> client_end;
  EXPECT_FALSE(client_end.is_valid());
}

TEST(ClientEnd, Control) {
  zx::channel h1, h2;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  zx_handle_t saved1 = h1.get();

  // Test initializing with channel.
  fidl::ClientEnd<llcpp_test::Frobinator> client_end(std::move(h1));
  EXPECT_TRUE(client_end.is_valid());
  EXPECT_EQ(saved1, client_end.channel().get());

  // Test move semantics.
  fidl::ClientEnd<llcpp_test::Frobinator> client_end_2 = std::move(client_end);
  EXPECT_FALSE(client_end.is_valid());
  EXPECT_TRUE(client_end_2.is_valid());
  EXPECT_EQ(saved1, client_end_2.channel().get());

  h1 = client_end_2.TakeChannel();
  EXPECT_EQ(saved1, h1.get());
  EXPECT_FALSE(client_end_2.is_valid());

  zx_handle_t saved2 = h2.get();
  client_end.channel() = std::move(h2);
  EXPECT_TRUE(client_end.is_valid());
  EXPECT_EQ(saved2, client_end.channel().get());

  // Test RAII channel management.
  client_end = {};
  EXPECT_FALSE(client_end.is_valid());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, h1.write(0, "a", 1, nullptr, 0));
}
