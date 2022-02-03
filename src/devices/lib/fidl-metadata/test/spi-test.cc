// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/fidl-metadata/spi.h"

#include <fidl/fuchsia.hardware.spi/cpp/wire.h>

#include <zxtest/zxtest.h>

static void check_encodes(cpp20::span<const fidl_metadata::spi::Channel> spi_channels) {
  // Encode.
  auto result = fidl_metadata::spi::SpiChannelsToFidl(spi_channels);
  ASSERT_OK(result.status_value());
  std::vector<uint8_t>& data = result.value();

  // Decode.
  // TODO(fxbug.dev/45252): Use FIDL at rest.
  fidl::DecodedMessage<fuchsia_hardware_spi::wire::SpiBusMetadata> decoded(
      fidl::internal::WireFormatVersion::kV1, data.data(), data.size());
  ASSERT_OK(decoded.status());

  auto metadata = decoded.PrimaryObject();

  // Check everything looks sensible.
  ASSERT_TRUE(metadata->has_channels());
  auto channels = metadata->channels();
  ASSERT_EQ(channels.count(), spi_channels.size());

  for (size_t i = 0; i < spi_channels.size(); i++) {
    ASSERT_TRUE(channels[i].has_bus_id());
    ASSERT_EQ(channels[i].bus_id(), spi_channels[i].bus_id);
    ASSERT_TRUE(channels[i].has_cs());
    ASSERT_EQ(channels[i].cs(), spi_channels[i].cs);
    if (spi_channels[i].did || spi_channels[i].vid || spi_channels[i].pid) {
      ASSERT_TRUE(channels[i].has_vid());
      ASSERT_EQ(channels[i].vid(), spi_channels[i].vid);
      ASSERT_TRUE(channels[i].has_pid());
      ASSERT_EQ(channels[i].pid(), spi_channels[i].pid);
      ASSERT_TRUE(channels[i].has_did());
      ASSERT_EQ(channels[i].did(), spi_channels[i].did);
    }
  }
}

TEST(SpiMetadataTest, TestEncodeNoPlatformIDs) {
  static constexpr fidl_metadata::spi::Channel kSpiChannels[] = {{
      .bus_id = 4,
      .cs = 0,
  }};

  ASSERT_NO_FATAL_FAILURE(check_encodes(kSpiChannels));
}

TEST(SpiMetadataTest, TestEncodeManyChannels) {
  static constexpr fidl_metadata::spi::Channel kSpiChannels[] = {
      {
          .bus_id = 1,
          .cs = 4,

          .vid = 10,
          .pid = 9,
          .did = 8,
      },
      {
          .bus_id = 0,
          .cs = 2,

          .vid = 8,
          .pid = 9,
          .did = 9,
      },
      {
          .bus_id = 92,
          .cs = 1,

          .vid = 0,
          .pid = 0,
          .did = 0,
      },
  };

  ASSERT_NO_FATAL_FAILURE(check_encodes(kSpiChannels));
}

TEST(SpiMetadataTest, TestEncodeNoChannels) {
  ASSERT_NO_FATAL_FAILURE(check_encodes(cpp20::span<const fidl_metadata::spi::Channel>()));
}
