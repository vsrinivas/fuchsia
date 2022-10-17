// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/llcpptest.protocol.test/cpp/wire.h>
#include <fidl/test.error.methods/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/wait.h>
#include <lib/fidl/cpp/wire/server.h>
#include <lib/fidl/cpp/wire/wire_messaging.h>
#include <zircon/errors.h>
#include <zircon/fidl.h>

#include <thread>

#include <zxtest/zxtest.h>

namespace test = ::llcpptest_protocol_test;

// These tests verify that the |status| and |reason| fields of the result
// of synchronous calls reflect the errors that happens in practice.

TEST(SyncClientErrorTest, PeerClosed) {
  zx::result endpoints = fidl::CreateEndpoints<test::EnumMethods>();
  ASSERT_OK(endpoints.status_value());
  fidl::WireSyncClient client{std::move(endpoints->client)};
  endpoints->server.reset();
  auto result = client->SendEnum(test::wire::MyError::kBadError);
  EXPECT_STATUS(ZX_ERR_PEER_CLOSED, result.status());
  EXPECT_EQ(fidl::Reason::kPeerClosed, result.reason());
}

TEST(SyncClientErrorTest, EncodeError) {
  zx::result endpoints = fidl::CreateEndpoints<test::EnumMethods>();
  ASSERT_OK(endpoints.status_value());
  fidl::WireSyncClient client{std::move(endpoints->client)};
  endpoints->server.reset();
  // Send the number 42 as |MyError|, will fail validation at send time.
  uint32_t bad_error = 42;
  static_assert(sizeof(bad_error) == sizeof(test::wire::MyError));
  auto result = client->SendEnum(static_cast<test::wire::MyError>(bad_error));
  EXPECT_STATUS(ZX_ERR_INVALID_ARGS, result.status());
  EXPECT_EQ(fidl::Reason::kEncodeError, result.reason());
  EXPECT_EQ(
      "FIDL operation failed due to encode error, status: ZX_ERR_INVALID_ARGS (-10), "
      "detail: not a valid enum value",
      result.FormatDescription());
}

TEST(SyncClientErrorTest, DecodeError) {
  zx::result endpoints = fidl::CreateEndpoints<test::EnumMethods>();
  ASSERT_OK(endpoints.status_value());
  std::thread replier{[&] {
    zx_signals_t observed;
    ASSERT_OK(
        endpoints->server.channel().wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), &observed));
    ASSERT_EQ(ZX_CHANNEL_READABLE, observed & ZX_CHANNEL_READABLE);
    fidl::internal::TransactionalRequest<test::EnumMethods::GetEnum> request{};
    uint32_t actual;
    endpoints->server.channel().read(0, &request, nullptr, sizeof(request), 0, &actual, nullptr);
    ASSERT_EQ(sizeof(request), actual);
    fidl::internal::TransactionalResponse<test::EnumMethods::GetEnum> message;

    // Zero the message body, to prevent hitting the "non-zero padding bytes" error, which is
    // checked before the "not valid enum member" error we're interested in
    memset(&message, 0, sizeof(message));

    // Send the number 42 as |MyError|, which will fail validation at the sync client when it
    // receives the message.
    fidl::InitTxnHeader(&message.header, request.header.txid, request.header.ordinal,
                        fidl::MessageDynamicFlags::kStrictMethod);
    message.body.e = static_cast<test::wire::MyError>(42);
    ASSERT_OK(endpoints->server.channel().write(0, reinterpret_cast<void*>(&message),
                                                sizeof(message), nullptr, 0));
  }};
  fidl::WireSyncClient client{std::move(endpoints->client)};
  auto result = client->GetEnum();
  replier.join();
  EXPECT_STATUS(ZX_ERR_INVALID_ARGS, result.status());
  EXPECT_EQ(fidl::Reason::kDecodeError, result.reason());
  EXPECT_EQ(
      "FIDL operation failed due to decode error, status: ZX_ERR_INVALID_ARGS (-10), "
      "detail: not a valid enum value",
      result.FormatDescription());
}

TEST(SyncClientErrorTest, DecodeErrorWithErrorSyntax) {
  zx::result endpoints = fidl::CreateEndpoints<test_error_methods::ErrorMethods>();
  ASSERT_OK(endpoints.status_value());
  std::thread replier{[&] {
    zx_signals_t observed;
    ASSERT_OK(
        endpoints->server.channel().wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), &observed));
    ASSERT_EQ(ZX_CHANNEL_READABLE, observed & ZX_CHANNEL_READABLE);
    fidl::internal::TransactionalRequest<test_error_methods::ErrorMethods::ManyArgsCustomError>
        request{};
    uint32_t actual;
    endpoints->server.channel().read(0, &request, nullptr, sizeof(request), 0, &actual, nullptr);
    ASSERT_EQ(sizeof(request), actual);
    fidl::internal::TransactionalResponse<test_error_methods::ErrorMethods::ManyArgsCustomError>
        message;

    // Zero the message body, to prevent hitting the "non-zero padding bytes" error, which is
    // checked before the "not valid enum member" error we're interested in
    memset(&message, 0, sizeof(message));

    // Send the number 42 as |MyError|, which will fail validation at the sync client when it
    // receives the message.
    fidl::InitTxnHeader(&message.header, request.header.txid, request.header.ordinal,
                        fidl::MessageDynamicFlags::kStrictMethod);
    message.body.result = test_error_methods::wire::ErrorMethodsManyArgsCustomErrorResult::WithErr(
        static_cast<test_error_methods::MyError>(42));
    ASSERT_OK(endpoints->server.channel().write(0, reinterpret_cast<void*>(&message),
                                                sizeof(message), nullptr, 0));
  }};
  fidl::WireSyncClient client{std::move(endpoints->client)};
  auto result = client->ManyArgsCustomError(true);
  replier.join();
  EXPECT_STATUS(ZX_ERR_INVALID_ARGS, result.status());
  EXPECT_EQ(fidl::Reason::kDecodeError, result.reason());
  EXPECT_EQ(
      "FIDL operation failed due to decode error, status: ZX_ERR_INVALID_ARGS (-10), "
      "detail: not a valid enum value",
      result.FormatDescription());
}
