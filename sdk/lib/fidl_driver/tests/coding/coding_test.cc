// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.driver.coding/cpp/driver/fidl.h>
#include <lib/fdf/cpp/channel.h>
#include <lib/zx/channel.h>

#include <utility>
#include <vector>

#include <zxtest/zxtest.h>

// TODO(fxbug.dev/99243): These are manual tests for driver endpoints and
// encoding over the driver transport. We should support them in GIDL.
namespace {

const std::vector<uint8_t> kGoldenBytes = {
    0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // table vector count
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // presence
    0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x01, 0x00,  // inlined handle 1
    0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x01, 0x00,  // inlined handle 2
    0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x01, 0x00,  // inlined handle 3
    0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x01, 0x00,  // inlined handle 4
    0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x01, 0x00,  // inlined handle 5
    0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x01, 0x00,  // inlined handle 6
};

TEST(StandaloneResourceTypeCoding, Encode) {
  // Set up an object.
  zx::channel zircon_handle_1, zircon_handle_2;
  ASSERT_OK(zx::channel::create(0, &zircon_handle_1, &zircon_handle_2));
  zx::status driver_handles = fdf::ChannelPair::Create(0);
  ASSERT_OK(driver_handles.status_value());
  zx::status zircon_endpoints = fidl::CreateEndpoints<test_driver_coding::ZirconChannelProtocol>();
  ASSERT_OK(zircon_endpoints.status_value());
  zx::status driver_endpoints = fdf::CreateEndpoints<test_driver_coding::DriverChannelProtocol>();
  ASSERT_OK(driver_endpoints.status_value());

  zx_handle_t expected_zircon_handle = zircon_handle_1.get();
  fdf_handle_t expected_driver_handle = driver_handles->end0.get();
  zx_handle_t expected_zircon_client_end = zircon_endpoints->client.handle()->get();
  zx_handle_t expected_zircon_server_end = zircon_endpoints->server.handle()->get();
  fdf_handle_t expected_driver_client_end = driver_endpoints->client.handle()->get();
  fdf_handle_t expected_driver_server_end = driver_endpoints->server.handle()->get();

  test_driver_coding::MixedResources obj{{
      .zircon_handle = std::move(zircon_handle_1),
      .driver_handle = std::move(driver_handles->end0),
      .zircon_client_end = std::move(zircon_endpoints->client),
      .zircon_server_end = std::move(zircon_endpoints->server),
      .driver_client_end = std::move(driver_endpoints->client),
      .driver_server_end = std::move(driver_endpoints->server),
  }};

  // Perform encoding.
  fidl::internal::EncodeResult encoded =
      fidl::internal::EncodeWithTransport<fidl::internal::DriverTransport,
                                          fidl::internal::EncodeResult>(std::move(obj));
  ASSERT_TRUE(encoded.message().ok(), "Error encoding: %s",
              encoded.message().error().FormatDescription().c_str());

  fidl::WireFormatMetadata metadata = encoded.wire_format_metadata();
  EXPECT_TRUE(metadata.is_valid());
  EXPECT_EQ(fidl::internal::WireFormatVersion::kV2, metadata.wire_format_version());

  fidl::OutgoingMessage::CopiedBytes bytes = encoded.message().CopyBytes();
  EXPECT_EQ(kGoldenBytes.size(), bytes.size());
  EXPECT_BYTES_EQ(kGoldenBytes.data(), bytes.data(), kGoldenBytes.size());

  const fidl_handle_t* handles = encoded.message().handles();
  EXPECT_EQ(expected_zircon_handle, handles[0]);
  EXPECT_EQ(expected_driver_handle, handles[1]);
  EXPECT_EQ(expected_zircon_client_end, handles[2]);
  EXPECT_EQ(expected_zircon_server_end, handles[3]);
  EXPECT_EQ(expected_driver_client_end, handles[4]);
  EXPECT_EQ(expected_driver_server_end, handles[5]);
}

TEST(StandaloneResourceTypeCoding, Decode) {
  // Set up some handles.
  zx::channel zircon_handle_1, zircon_handle_2;
  ASSERT_OK(zx::channel::create(0, &zircon_handle_1, &zircon_handle_2));
  zx::status driver_handles = fdf::ChannelPair::Create(0);
  ASSERT_OK(driver_handles.status_value());
  zx::status zircon_endpoints = fidl::CreateEndpoints<test_driver_coding::ZirconChannelProtocol>();
  ASSERT_OK(zircon_endpoints.status_value());
  zx::status driver_endpoints = fdf::CreateEndpoints<test_driver_coding::DriverChannelProtocol>();
  ASSERT_OK(driver_endpoints.status_value());

  zx_handle_t expected_zircon_handle = zircon_handle_1.release();
  fdf_handle_t expected_driver_handle = driver_handles->end0.release();
  zx_handle_t expected_zircon_client_end = zircon_endpoints->client.TakeHandle().release();
  zx_handle_t expected_zircon_server_end = zircon_endpoints->server.TakeHandle().release();
  fdf_handle_t expected_driver_client_end = driver_endpoints->client.TakeHandle().release();
  fdf_handle_t expected_driver_server_end = driver_endpoints->server.TakeHandle().release();

  std::vector<uint8_t> bytes = kGoldenBytes;
  std::vector<fidl_handle_t> handles = {
      expected_zircon_handle,     expected_driver_handle,     expected_zircon_client_end,
      expected_zircon_server_end, expected_driver_client_end, expected_driver_server_end,
  };
  fidl::WireFormatMetadata metadata =
      fidl::internal::WireFormatMetadataForVersion(fidl::internal::WireFormatVersion::kV2);
  fidl::EncodedMessage message = fidl::EncodedMessage::Create<fidl::internal::DriverTransport>(
      cpp20::span(bytes), handles.data(), nullptr, static_cast<uint32_t>(handles.size()));

  // Perform decoding.
  fit::result decoded =
      fidl::Decode<test_driver_coding::MixedResources>(std::move(message), metadata);
  ASSERT_TRUE(decoded.is_ok());

  test_driver_coding::MixedResources& value = decoded.value();
  EXPECT_EQ(expected_zircon_handle, value.zircon_handle()->get());
  EXPECT_EQ(expected_driver_handle, value.driver_handle()->get());
  EXPECT_EQ(expected_zircon_client_end, value.zircon_client_end()->handle()->get());
  EXPECT_EQ(expected_zircon_server_end, value.zircon_server_end()->handle()->get());
  EXPECT_EQ(expected_driver_client_end, value.driver_client_end()->handle()->get());
  EXPECT_EQ(expected_driver_server_end, value.driver_server_end()->handle()->get());
}

}  // namespace
