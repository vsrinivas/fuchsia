// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/wait.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/fidl/llcpp/wire_messaging.h>

#include <thread>

#include <gtest/gtest.h>
#include <llcpptest/protocol/test/llcpp/fidl.h>

namespace test = ::llcpptest_protocol_test;

// These tests verify that the |status| and |reason| fields of the result
// of synchronous calls reflect the errors that happens in practice.

TEST(SyncClientErrorTest, PeerClosed) {
  zx::status endpoints = fidl::CreateEndpoints<test::EnumMethods>();
  ASSERT_EQ(ZX_OK, endpoints.status_value());
  auto client = fidl::BindSyncClient(std::move(endpoints->client));
  endpoints->server.reset();
  auto result = client.SendEnum(test::wire::MyError::kBadError);
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, result.status());
  EXPECT_EQ(fidl::Reason::kPeerClosed, result.reason());
}

TEST(SyncClientErrorTest, EncodeError) {
  zx::status endpoints = fidl::CreateEndpoints<test::EnumMethods>();
  ASSERT_EQ(ZX_OK, endpoints.status_value());
  auto client = fidl::BindSyncClient(std::move(endpoints->client));
  endpoints->server.reset();
  // Send the number 42 as |MyError|, will fail validation at send time.
  uint32_t bad_error = 42;
  static_assert(sizeof(bad_error) == sizeof(test::wire::MyError));
  auto result = client.SendEnum(static_cast<test::wire::MyError>(bad_error));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, result.status());
  EXPECT_EQ(fidl::Reason::kEncodeError, result.reason());
  EXPECT_EQ(
      "FIDL operation failed due to encode error, status: ZX_ERR_INVALID_ARGS (-10), "
      "detail: not a valid enum member",
      result.FormatDescription());
}

TEST(SyncClientErrorTest, DecodeError) {
  zx::status endpoints = fidl::CreateEndpoints<test::EnumMethods>();
  ASSERT_EQ(ZX_OK, endpoints.status_value());
  std::thread replier{[&] {
    zx_signals_t observed;
    ASSERT_EQ(ZX_OK, endpoints->server.channel().wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(),
                                                          &observed));
    ASSERT_EQ(ZX_CHANNEL_READABLE, observed & ZX_CHANNEL_READABLE);
    fidl::WireRequest<test::EnumMethods::GetEnum> request{};
    uint32_t actual;
    endpoints->server.channel().read(0, &request, nullptr, sizeof(request), 0, &actual, nullptr);
    ASSERT_EQ(sizeof(request), actual);
    fidl::WireResponse<test::EnumMethods::GetEnum> message;
    fidl_init_txn_header(&message._hdr, request._hdr.txid, request._hdr.ordinal);
    // Send the number 42 as |MyError|, will fail validation at the sync client
    // when it receives the message.
    message.e = static_cast<test::wire::MyError>(42);
    ASSERT_EQ(ZX_OK, endpoints->server.channel().write(0, reinterpret_cast<void*>(&message),
                                                       sizeof(message), nullptr, 0));
  }};
  auto client = fidl::BindSyncClient(std::move(endpoints->client));
  auto result = client.GetEnum();
  replier.join();
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, result.status());
  EXPECT_EQ(fidl::Reason::kDecodeError, result.reason());
  EXPECT_EQ(
      "FIDL operation failed due to decode error, status: ZX_ERR_INVALID_ARGS (-10), "
      "detail: not a valid enum member",
      result.FormatDescription());
}
