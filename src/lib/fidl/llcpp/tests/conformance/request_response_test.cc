// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains manual test cases that should be migrated to GIDL
// and be generated as part of conformance_test.cc in the future.

#include <fidl/test.types/cpp/wire.h>

#ifndef __Fuchsia__
#include <lib/fidl/cpp/wire/internal/transport_channel_host.h>
#endif

#include <gtest/gtest.h>

TEST(FidlHost, Request) {
  // Argument for the request.
  test_types::wire::FooRequest req;
  req.bar = 10;
  // The request.
  fidl::internal::TransactionalRequest<test_types::Baz::Foo> foo(req);
  // Serialized version of the request.
  fidl::unstable::OwnedEncodedMessage<fidl::internal::TransactionalRequest<test_types::Baz::Foo>>
      message(&foo);
  EXPECT_EQ(message.status(), ZX_OK);
  // Linear byte buffer for the request.
  auto bytes = message.GetOutgoingMessage().CopyBytes();
  EXPECT_EQ(bytes.size(), 24U);
  // Decoded version of the linear buffer.
  fit::result decoded =
      fidl::internal::InplaceDecodeTransactionalMessage<test_types::wire::BazFooRequest>(
          fidl::IncomingHeaderAndMessage::Create<fidl::internal::ChannelTransport>(
              bytes.data(), bytes.size(), nullptr, nullptr, 0));
  ASSERT_TRUE(decoded.is_ok());
  // Checks that the decoded version is equivalent to the original.
  EXPECT_EQ(decoded->req.bar, req.bar);
}

TEST(FidlHost, Response) {
  // Argument for the response.
  test_types::wire::FooResponse res;
  res.bar = 10;
  // The response.
  fidl::internal::TransactionalResponse<test_types::Baz::Foo> foo(res);
  // Serialized version of the response.
  fidl::unstable::OwnedEncodedMessage<fidl::internal::TransactionalResponse<test_types::Baz::Foo>>
      message(&foo);
  EXPECT_EQ(message.status(), ZX_OK);
  // Linear byte buffer for the response.
  auto bytes = message.GetOutgoingMessage().CopyBytes();
  EXPECT_EQ(bytes.size(), 24U);
  // Decoded version of the linear buffer.
  fit::result decoded =
      fidl::internal::InplaceDecodeTransactionalMessage<fidl::WireResponse<test_types::Baz::Foo>>(
          fidl::IncomingHeaderAndMessage::Create<fidl::internal::ChannelTransport>(
              bytes.data(), bytes.size(), nullptr, nullptr, 0));
  ASSERT_TRUE(decoded.is_ok());
  // Checks that the decoded version is equivalent to the original.
  EXPECT_EQ(decoded->res.bar, res.bar);
}
