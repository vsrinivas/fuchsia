// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/llcpptest.protocol.test/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/llcpp/client_end.h>
#include <lib/zx/object.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls/object.h>

#include <type_traits>

#include <gtest/gtest.h>

namespace llcpp_test = ::llcpptest_protocol_test;

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

TEST(ClientEnd, Comparisons) {
  auto endpoints = fidl::CreateEndpoints<llcpp_test::Frobinator>();
  ASSERT_EQ(ZX_OK, endpoints.status_value()) << endpoints.status_string();
  fidl::ClientEnd<llcpp_test::Frobinator> client_end = std::move(endpoints->client);
  const fidl::ClientEnd<llcpp_test::Frobinator>& client_end_reference = client_end;
  ASSERT_EQ(client_end, client_end);
  ASSERT_EQ(client_end, client_end_reference);

  fidl::ClientEnd<llcpp_test::Frobinator> invalid1{};
  fidl::ClientEnd<llcpp_test::Frobinator> invalid2{};
  ASSERT_EQ(invalid1, invalid2);
  ASSERT_NE(client_end, invalid1);

  // We assume that an invalid handle always have the value 0, hence smaller
  // than any valid handle.
  static_assert(ZX_HANDLE_INVALID == 0);
  ASSERT_GT(client_end, invalid1);
  ASSERT_LT(invalid1, client_end);
}

TEST(UnownedClientEnd, Constructors) {
  auto endpoints = fidl::CreateEndpoints<llcpp_test::Frobinator>();
  ASSERT_EQ(ZX_OK, endpoints.status_value()) << endpoints.status_string();
  fidl::ClientEnd<llcpp_test::Frobinator> client_end = std::move(endpoints->client);

  {
    // Construct from a |fidl::ClientEnd|.
    fidl::UnownedClientEnd<llcpp_test::Frobinator> unowned_client_end(client_end);
    ASSERT_EQ(unowned_client_end.channel(), client_end.channel().get());

    // Implicit construction during parameter passing.
    auto id = [](fidl::UnownedClientEnd<llcpp_test::Frobinator> unowned) { return unowned; };
    auto roundtrip = id(client_end);
    ASSERT_EQ(roundtrip.channel(), client_end.channel().get());
  }

  {
    // Construct from a |zx_handle_t|.
    fidl::UnownedClientEnd<llcpp_test::Frobinator> unowned_client_end(client_end.channel().get());
    ASSERT_EQ(unowned_client_end.channel(), client_end.channel().get());
  }

  {
    // Copy construction.
    fidl::UnownedClientEnd<llcpp_test::Frobinator> unowned_client_end(client_end);
    fidl::UnownedClientEnd<llcpp_test::Frobinator> unowned_client_end2(unowned_client_end);
    ASSERT_EQ(unowned_client_end.channel(), unowned_client_end2.channel());
  }
}

TEST(UnownedClientEnd, IsValid) {
  fidl::ClientEnd<llcpp_test::Frobinator> invalid{};
  fidl::UnownedClientEnd<llcpp_test::Frobinator> unowned_client_end(invalid);
  ASSERT_FALSE(unowned_client_end.is_valid());

  auto endpoints = fidl::CreateEndpoints<llcpp_test::Frobinator>();
  ASSERT_EQ(ZX_OK, endpoints.status_value()) << endpoints.status_string();
  fidl::UnownedClientEnd<llcpp_test::Frobinator> unowned_client_end_valid(endpoints->client);
  ASSERT_TRUE(unowned_client_end_valid.is_valid());
}

TEST(UnownedClientEnd, BorrowFromClientEnd) {
  auto endpoints = fidl::CreateEndpoints<llcpp_test::Frobinator>();
  ASSERT_EQ(ZX_OK, endpoints.status_value()) << endpoints.status_string();

  auto unowned_client_end = endpoints->client.borrow();
  static_assert(std::is_same_v<decltype(unowned_client_end),
                               decltype(fidl::UnownedClientEnd<llcpp_test::Frobinator>(0))>);
  ASSERT_EQ(unowned_client_end.channel(), endpoints->client.channel().get());
}

TEST(UnownedClientEnd, Comparisons) {
  // Because unowned client ends do not own their wrapped handles,
  // we may use placeholder handle values in this unit test.
  static_assert(std::is_trivially_destructible_v<fidl::UnownedClientEnd<llcpp_test::Frobinator>>);
  fidl::UnownedClientEnd<llcpp_test::Frobinator> one(1);
  fidl::UnownedClientEnd<llcpp_test::Frobinator> small(1);
  fidl::UnownedClientEnd<llcpp_test::Frobinator> large(42);
  ASSERT_EQ(small, small);
  ASSERT_EQ(one, small);
  ASSERT_NE(small, large);
  ASSERT_GT(large, small);
  ASSERT_LT(small, large);
}
