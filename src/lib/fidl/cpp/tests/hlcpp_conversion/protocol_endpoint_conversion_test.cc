// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.types/cpp/hlcpp_conversion.h>
#include <lib/fidl/cpp/wire/channel.h>

#include <gtest/gtest.h>

#ifndef __Fuchsia__
#error This test only makes sense on Fuchsia.
#endif

TEST(ProtocolEndpointConversion, ToNatural) {
  fidl::InterfaceHandle<test::types::Baz> hlcpp_client;
  fidl::InterfaceRequest<test::types::Baz> hlcpp_server = hlcpp_client.NewRequest();

  EXPECT_TRUE(hlcpp_client.is_valid());
  EXPECT_TRUE(hlcpp_server.is_valid());
  zx_handle_t hlcpp_client_handle = hlcpp_client.channel().get();
  zx_handle_t hlcpp_server_handle = hlcpp_server.channel().get();

  auto unified_client = fidl::HLCPPToNatural(std::move(hlcpp_client));
  static_assert(std::is_same_v<decltype(unified_client), fidl::ClientEnd<test_types::Baz>>);
  EXPECT_FALSE(hlcpp_client.is_valid());
  EXPECT_TRUE(unified_client.is_valid());
  EXPECT_EQ(hlcpp_client_handle, unified_client.handle()->get());

  auto unified_server = fidl::HLCPPToNatural(std::move(hlcpp_server));
  static_assert(std::is_same_v<decltype(unified_server), fidl::ServerEnd<test_types::Baz>>);
  EXPECT_FALSE(hlcpp_server.is_valid());
  EXPECT_TRUE(unified_server.is_valid());
  EXPECT_EQ(hlcpp_server_handle, unified_server.handle()->get());
}

TEST(ProtocolEndpointConversion, ToHLCPP) {
  zx::result unified_endpoints = fidl::CreateEndpoints<test_types::Baz>();
  ASSERT_TRUE(unified_endpoints.is_ok());

  EXPECT_TRUE(unified_endpoints->client.is_valid());
  EXPECT_TRUE(unified_endpoints->server.is_valid());
  zx_handle_t unified_client_handle = unified_endpoints->client.handle()->get();
  zx_handle_t unified_server_handle = unified_endpoints->server.handle()->get();

  auto hlcpp_client = fidl::NaturalToHLCPP(std::move(unified_endpoints->client));
  static_assert(std::is_same_v<decltype(hlcpp_client), fidl::InterfaceHandle<test::types::Baz>>);
  EXPECT_FALSE(unified_endpoints->client.is_valid());
  EXPECT_TRUE(hlcpp_client.is_valid());
  EXPECT_EQ(unified_client_handle, hlcpp_client.channel().get());

  auto hlcpp_server = fidl::NaturalToHLCPP(std::move(unified_endpoints->server));
  static_assert(std::is_same_v<decltype(hlcpp_server), fidl::InterfaceRequest<test::types::Baz>>);
  EXPECT_FALSE(unified_endpoints->server.is_valid());
  EXPECT_TRUE(hlcpp_server.is_valid());
  EXPECT_EQ(unified_server_handle, hlcpp_server.channel().get());
}
