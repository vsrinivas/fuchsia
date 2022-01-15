// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.transport/cpp/driver/wire.h>

#include <zxtest/zxtest.h>

// Test creating a typed channel endpoint pair.
TEST(Endpoints, CreateFromProtocol) {
  // `std::move` pattern
  {
    auto endpoints = fdf::CreateEndpoints<test_transport::TwoWayTest>();
    ASSERT_OK(endpoints.status_value());
    ASSERT_EQ(ZX_OK, endpoints.status_value());
    fdf::ClientEnd<test_transport::TwoWayTest> client_end = std::move(endpoints->client);
    fdf::ServerEnd<test_transport::TwoWayTest> server_end = std::move(endpoints->server);

    ASSERT_TRUE(client_end.is_valid());
    ASSERT_TRUE(server_end.is_valid());
  }

  // Destructuring pattern
  {
    auto endpoints = fdf::CreateEndpoints<test_transport::TwoWayTest>();
    ASSERT_OK(endpoints.status_value());
    ASSERT_EQ(ZX_OK, endpoints.status_value());
    auto [client_end, server_end] = std::move(endpoints.value());

    ASSERT_TRUE(client_end.is_valid());
    ASSERT_TRUE(server_end.is_valid());
  }
}

// Test creating a typed channel endpoint pair using the out-parameter
// overloads.
TEST(Endpoints, CreateFromProtocolOutParameterStyleClientRetained) {
  fdf::ClientEnd<test_transport::TwoWayTest> client_end;
  auto server_end = fdf::CreateEndpoints(&client_end);
  ASSERT_OK(server_end.status_value());
  ASSERT_EQ(ZX_OK, server_end.status_value());

  ASSERT_TRUE(client_end.is_valid());
  ASSERT_TRUE(server_end->is_valid());
}

TEST(Endpoints, CreateFromProtocolOutParameterStyleServerRetained) {
  fdf::ServerEnd<test_transport::TwoWayTest> server_end;
  auto client_end = fdf::CreateEndpoints(&server_end);
  ASSERT_OK(client_end.status_value());
  ASSERT_EQ(ZX_OK, client_end.status_value());

  ASSERT_TRUE(server_end.is_valid());
  ASSERT_TRUE(client_end->is_valid());
}
