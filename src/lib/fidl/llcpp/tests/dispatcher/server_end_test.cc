// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/llcpptest.protocol.test/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/fidl/cpp/wire/server.h>
#include <lib/zx/object.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls/object.h>

#include <gtest/gtest.h>

namespace llcpp_test = ::llcpptest_protocol_test;

TEST(ServerEnd, Trivial) {
  fidl::ServerEnd<llcpp_test::Frobinator> server_end;
  EXPECT_FALSE(server_end.is_valid());
}

TEST(ServerEnd, Control) {
  zx::channel h1, h2;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  zx_handle_t saved1 = h1.get();

  // Test initializing with channel.
  fidl::ServerEnd<llcpp_test::Frobinator> server_end(std::move(h1));
  EXPECT_TRUE(server_end.is_valid());
  EXPECT_EQ(saved1, server_end.channel().get());

  // Test move semantics.
  fidl::ServerEnd<llcpp_test::Frobinator> server_end_2 = std::move(server_end);
  EXPECT_FALSE(server_end.is_valid());
  EXPECT_TRUE(server_end_2.is_valid());
  EXPECT_EQ(saved1, server_end_2.channel().get());

  h1 = server_end_2.TakeChannel();
  EXPECT_EQ(saved1, h1.get());
  EXPECT_FALSE(server_end_2.is_valid());

  zx_handle_t saved2 = h2.get();
  server_end.channel() = std::move(h2);
  EXPECT_TRUE(server_end.is_valid());
  EXPECT_EQ(saved2, server_end.channel().get());

  // Test RAII channel management.
  server_end = {};
  EXPECT_FALSE(server_end.is_valid());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, h1.write(0, "a", 1, nullptr, 0));
}

TEST(ServerEnd, Close) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto endpoints = fidl::CreateEndpoints<llcpp_test::Frobinator>();
  ASSERT_EQ(endpoints.status_value(), ZX_OK);

  class EventHandler : public fidl::WireAsyncEventHandler<llcpp_test::Frobinator> {
   public:
    EventHandler() = default;

    fidl::UnbindInfo recorded_unbind_info() const { return recorded_unbind_info_; }

    void on_fidl_error(fidl::UnbindInfo unbind_info) override {
      recorded_unbind_info_ = unbind_info;
    }

   private:
    fidl::UnbindInfo recorded_unbind_info_;
  };

  EventHandler event_handler;
  fidl::WireClient client(std::move(endpoints->client), loop.dispatcher(), &event_handler);

  fidl::ServerEnd<llcpp_test::Frobinator> server_end(std::move(endpoints->server));
  EXPECT_TRUE(server_end.is_valid());

  constexpr zx_status_t kSysError = ZX_ERR_INVALID_ARGS;
  EXPECT_EQ(ZX_OK, server_end.Close(kSysError));
  EXPECT_FALSE(server_end.is_valid());

  loop.RunUntilIdle();
  EXPECT_EQ(fidl::Reason::kPeerClosed, event_handler.recorded_unbind_info().reason());
  EXPECT_EQ(kSysError, event_handler.recorded_unbind_info().status());
}

TEST(ServerEnd, CloseTwice) {
  zx::channel h1, h2;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  fidl::ServerEnd<llcpp_test::Frobinator> server_end(std::move(h2));
  EXPECT_EQ(ZX_OK, server_end.Close(ZX_OK));

  ASSERT_DEATH(server_end.Close(ZX_OK), "Cannot close an invalid ServerEnd.");
}

TEST(UnownedServerEnd, Constructors) {
  auto endpoints = fidl::CreateEndpoints<llcpp_test::Frobinator>();
  ASSERT_EQ(ZX_OK, endpoints.status_value()) << endpoints.status_string();
  fidl::ServerEnd<llcpp_test::Frobinator> server_end = std::move(endpoints->server);

  {
    // Construct from a |fidl::ServerEnd|.
    fidl::UnownedServerEnd<llcpp_test::Frobinator> unowned_server_end(server_end);
    ASSERT_EQ(unowned_server_end.handle(), server_end.channel().get());

    // Implicit construction during parameter passing.
    auto id = [](fidl::UnownedServerEnd<llcpp_test::Frobinator> unowned) { return unowned; };
    auto roundtrip = id(server_end);
    ASSERT_EQ(roundtrip.handle(), server_end.channel().get());
  }

  {
    // Construct from a |zx_handle_t|.
    fidl::UnownedServerEnd<llcpp_test::Frobinator> unowned_server_end(server_end.channel().get());
    ASSERT_EQ(unowned_server_end.handle(), server_end.channel().get());
  }

  {
    // Copy construction.
    fidl::UnownedServerEnd<llcpp_test::Frobinator> unowned_server_end(server_end);
    fidl::UnownedServerEnd<llcpp_test::Frobinator> unowned_server_end2(unowned_server_end);
    ASSERT_EQ(unowned_server_end.handle(), unowned_server_end2.handle());
  }
}

TEST(UnownedServerEnd, IsValid) {
  fidl::ServerEnd<llcpp_test::Frobinator> invalid{};
  fidl::UnownedServerEnd<llcpp_test::Frobinator> unowned_server_end(invalid);
  ASSERT_FALSE(unowned_server_end.is_valid());

  auto endpoints = fidl::CreateEndpoints<llcpp_test::Frobinator>();
  ASSERT_EQ(ZX_OK, endpoints.status_value()) << endpoints.status_string();
  fidl::UnownedServerEnd<llcpp_test::Frobinator> unowned_server_end_valid(endpoints->server);
  ASSERT_TRUE(unowned_server_end_valid.is_valid());
}

TEST(UnownedServerEnd, BorrowFromServerEnd) {
  auto endpoints = fidl::CreateEndpoints<llcpp_test::Frobinator>();
  ASSERT_EQ(ZX_OK, endpoints.status_value()) << endpoints.status_string();

  auto unowned_server_end = endpoints->client.borrow();
  static_assert(std::is_same_v<decltype(unowned_server_end),
                               decltype(fidl::UnownedClientEnd<llcpp_test::Frobinator>(0))>);
  ASSERT_EQ(unowned_server_end.channel(), endpoints->client.channel().get());
}
