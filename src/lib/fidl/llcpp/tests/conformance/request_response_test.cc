// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains manual test cases that should be migrated to GIDL
// and be generated as part of conformance_test.cc in the future.

#include <fidl/fidl.llcpp.types.test/cpp/wire.h>

#include <gtest/gtest.h>

TEST(FidlHost, Request) {
  // Argument for the request.
  fidl_llcpp_types_test::wire::FooRequest req;
  req.bar = 10;
  // The request.
  fidl::WireRequest<fidl_llcpp_types_test::Baz::Foo> foo(req);
  // Serialized version of the request.
  fidl::OwnedEncodedMessage<fidl::WireRequest<fidl_llcpp_types_test::Baz::Foo>> message(&foo);
  EXPECT_EQ(message.status(), ZX_OK);
  // Linear byte buffer for the request.
  auto bytes = message.GetOutgoingMessage().CopyBytes();
  EXPECT_EQ(bytes.size(), 24U);
  // Decoded version of the linear buffer.
  fidl::DecodedMessage<fidl::WireRequest<fidl_llcpp_types_test::Baz::Foo>> decoded(bytes.data(),
                                                                                   bytes.size());
  // Checks that the decoded version is equivalent to the original.
  EXPECT_EQ(decoded.PrimaryObject()->req.bar, req.bar);
}

TEST(FidlHost, Response) {
  // Argument for the response.
  fidl_llcpp_types_test::wire::FooResponse res;
  res.bar = 10;
  // The response.
  fidl::WireResponse<fidl_llcpp_types_test::Baz::Foo> foo(res);
  // Serialized version of the response.
  fidl::OwnedEncodedMessage<fidl::WireResponse<fidl_llcpp_types_test::Baz::Foo>> message(&foo);
  EXPECT_EQ(message.status(), ZX_OK);
  // Linear byte buffer for the response.
  auto bytes = message.GetOutgoingMessage().CopyBytes();
  EXPECT_EQ(bytes.size(), 24U);
  // Decoded version of the linear buffer.
  fidl::DecodedMessage<fidl::WireResponse<fidl_llcpp_types_test::Baz::Foo>> decoded(bytes.data(),
                                                                                    bytes.size());
  // Checks that the decoded version is equivalent to the original.
  EXPECT_EQ(decoded.PrimaryObject()->res.bar, res.bar);
}
