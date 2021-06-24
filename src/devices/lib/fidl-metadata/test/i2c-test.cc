// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/fidl-metadata/i2c.h"

#include <fuchsia/hardware/i2c/llcpp/fidl.h>

#include <zxtest/zxtest.h>

static void check_encodes(const cpp20::span<const fidl_metadata::i2c::Channel> i2c_channels) {
  // Encode.
  auto result = fidl_metadata::i2c::I2CChannelsToFidl(i2c_channels);
  ASSERT_OK(result.status_value());
  std::vector<uint8_t>& data = result.value();

  // Decode.
  fidl::DecodedMessage<fuchsia_hardware_i2c::wire::I2CBusMetadata> decoded(data.data(),
                                                                           data.size());
  ASSERT_OK(decoded.status());

  auto metadata = decoded.PrimaryObject();

  // Check everything looks sensible.
  ASSERT_TRUE(metadata->has_channels());
  auto channels = metadata->channels();
  ASSERT_EQ(channels.count(), i2c_channels.size());

  for (size_t i = 0; i < i2c_channels.size(); i++) {
    ASSERT_TRUE(channels[i].has_bus_id());
    ASSERT_EQ(channels[i].bus_id(), i2c_channels[i].bus_id);
    ASSERT_TRUE(channels[i].has_address());
    ASSERT_EQ(channels[i].address(), i2c_channels[i].address);
    if (i2c_channels[i].did || i2c_channels[i].vid || i2c_channels[i].pid) {
      ASSERT_TRUE(channels[i].has_vid());
      ASSERT_EQ(channels[i].vid(), i2c_channels[i].vid);
      ASSERT_TRUE(channels[i].has_pid());
      ASSERT_EQ(channels[i].pid(), i2c_channels[i].pid);
      ASSERT_TRUE(channels[i].has_did());
      ASSERT_EQ(channels[i].did(), i2c_channels[i].did);
    }
  }
}

TEST(I2cMetadataTest, TestEncodeNoPlatformIDs) {
  static constexpr fidl_metadata::i2c::Channel kI2cChannels[] = {{
      .bus_id = 4,
      .address = 0x01,
  }};

  ASSERT_NO_FATAL_FAILURES(check_encodes(kI2cChannels));
}

TEST(I2cMetadataTest, TestEncodeManyChannels) {
  static constexpr fidl_metadata::i2c::Channel kI2cChannels[] = {
      {
          .bus_id = 1,
          .address = 0x49,

          .vid = 10,
          .pid = 9,
          .did = 8,
      },
      {
          .bus_id = 0,
          .address = 0x47,

          .vid = 8,
          .pid = 9,
          .did = 9,
      },
      {
          .bus_id = 92,
          .address = 0xaa,

          .vid = 0,
          .pid = 0,
          .did = 0,
      },
  };

  ASSERT_NO_FATAL_FAILURES(check_encodes(kI2cChannels));
}

TEST(I2cMetadataTest, TestEncodeNoChannels) {
  ASSERT_NO_FATAL_FAILURES(check_encodes(cpp20::span<const fidl_metadata::i2c::Channel>()));
}
