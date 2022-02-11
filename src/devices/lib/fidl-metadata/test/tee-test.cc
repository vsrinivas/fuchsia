// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/fidl-metadata/tee.h"

#include <fidl/fuchsia.hardware.tee/cpp/wire.h>

#include <zxtest/zxtest.h>

using tee_thread_config_t = fidl_metadata::tee::CustomThreadConfig;

static void check_encodes(
    uint32_t thread_count,
    const cpp20::span<const fidl_metadata::tee::CustomThreadConfig> thread_configs) {
  // Encode.
  auto result = fidl_metadata::tee::TeeMetadataToFidl(thread_count, thread_configs);
  ASSERT_OK(result.status_value());
  std::vector<uint8_t>& data = result.value();

  // Decode.
  fidl::unstable::DecodedMessage<fuchsia_hardware_tee::wire::TeeMetadata> decoded(data.data(),
                                                                                  data.size());
  ASSERT_OK(decoded.status());

  auto metadata = decoded.PrimaryObject();

  // Check everything looks sensible.
  ASSERT_TRUE(metadata->has_custom_threads());
  ASSERT_TRUE(metadata->has_default_thread_count());
  ASSERT_EQ(metadata->default_thread_count(), thread_count);

  auto configs = metadata->custom_threads();
  ASSERT_EQ(configs.count(), thread_configs.size());

  for (size_t i = 0; i < thread_configs.size(); i++) {
    ASSERT_TRUE(configs[i].has_role());
    ASSERT_TRUE(configs[i].has_count());
    ASSERT_EQ(std::string(configs[i].role().data()), thread_configs[i].role);
    ASSERT_EQ(configs[i].count(), thread_configs[i].count);

    ASSERT_TRUE(configs[i].has_trusted_apps());
    ASSERT_EQ(configs[i].trusted_apps().count(), thread_configs[i].trusted_apps.size());

    auto trusted_apps = configs[i].trusted_apps();
    for (size_t j = 0; j < thread_configs[i].trusted_apps.size(); ++j) {
      ASSERT_EQ(trusted_apps[j].time_low, thread_configs[i].trusted_apps[j].time_low);
      ASSERT_EQ(trusted_apps[j].time_mid, thread_configs[i].trusted_apps[j].time_mid);
      ASSERT_EQ(trusted_apps[j].time_hi_and_version,
                thread_configs[i].trusted_apps[j].time_hi_and_version);
      ASSERT_EQ(memcmp(trusted_apps[j].clock_seq_and_node.data(),
                       thread_configs[i].trusted_apps[j].clock_seq_and_node, 8),
                0);
    }
  }
}

TEST(TeeMetadataTest, TestEncodeNoTrustedApps) {
  static tee_thread_config_t tee_thread_cfg[] = {{"fuchsia.tee.media", 1, {}}};

  ASSERT_NO_FATAL_FAILURE(check_encodes(1, tee_thread_cfg));
}

TEST(TeeMetadataTest, TestEncodeManyThreads) {
  static tee_thread_config_t tee_thread_cfg[] = {
      {"fuchsia.tee.pool1",
       1,
       {{0x01020304, 0x0000, 0x1234, {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}},
        {0x01020304, 0x0001, 0x1235, {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}}}},
      {"fuchsia.tee.pool2",
       2,
       {{0x01020304, 0x1000, 0x1234, {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}},
        {0x01020304, 0x1001, 0x1235, {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}}}}};

  ASSERT_NO_FATAL_FAILURE(check_encodes(1, tee_thread_cfg));
}

TEST(TeeMetadataTest, TestEncodeNoCustomThreads) {
  ASSERT_NO_FATAL_FAILURE(
      check_encodes(1, cpp20::span<const fidl_metadata::tee::CustomThreadConfig>()));
}
