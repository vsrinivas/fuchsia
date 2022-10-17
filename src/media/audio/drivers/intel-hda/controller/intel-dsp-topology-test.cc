// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-dsp-topology.h"

#include <lib/ddk/debug.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <zxtest/zxtest.h>

#include "debug-logging.h"
#include "example-nhlt-data.h"

namespace audio::intel_hda {
namespace {

TEST(DspTopology, GetI2sBlob) {
  zx::result<std::unique_ptr<Nhlt>> nhlt = Nhlt::FromBuffer(kExampleNhtl);
  ASSERT_TRUE(nhlt.is_ok());

  AudioDataFormat format = {
      .sampling_frequency = SamplingFrequency::FS_48000HZ,
      .bit_depth = BitDepth::DEPTH_16BIT,
      .channel_map = 0xFFFFFF10,
      .channel_config = ChannelConfig::CONFIG_STEREO,
      .interleaving_style = InterleavingStyle::PER_CHANNEL,
      .number_of_channels = 2,
      .valid_bit_depth = 16,
      .sample_type = SampleType::INT_MSB,
      .reserved = 0,
  };

  const void* data;
  size_t data_size = 0;
  constexpr uint8_t kBusId = 0;
  ASSERT_OK(GetNhltBlob(*nhlt.value().get(), kBusId, NHLT_DIRECTION_RENDER, NHLT_LINK_TYPE_SSP,
                        format, &data, &data_size));
}

TEST(DspTopology, GetPdmBlob) {
  zx::result<std::unique_ptr<Nhlt>> nhlt = Nhlt::FromBuffer(kExampleNhtl);
  ASSERT_TRUE(nhlt.is_ok());

  AudioDataFormat format = {
      .sampling_frequency = SamplingFrequency::FS_48000HZ,
      .bit_depth = BitDepth::DEPTH_16BIT,
      .channel_map = 0xFFFF3210,
      .channel_config = ChannelConfig::CONFIG_QUATRO,
      .interleaving_style = InterleavingStyle::PER_CHANNEL,
      .number_of_channels = 4,
      .valid_bit_depth = 16,
      .sample_type = SampleType::INT_MSB,
      .reserved = 0,
  };

  const void* data;
  size_t data_size = 0;
  constexpr uint8_t kBusId = 0;
  ASSERT_OK(GetNhltBlob(*nhlt.value().get(), kBusId, NHLT_DIRECTION_CAPTURE, NHLT_LINK_TYPE_PDM,
                        format, &data, &data_size));
}

TEST(DspTopology, GetI2sModuleConfig) {
  zx::result<std::unique_ptr<Nhlt>> nhlt = Nhlt::FromBuffer(kExampleNhtl);
  ASSERT_TRUE(nhlt.is_ok());

  AudioDataFormat format = {
      .sampling_frequency = SamplingFrequency::FS_48000HZ,
      .bit_depth = BitDepth::DEPTH_16BIT,
      .channel_map = 0xFFFFFF10,
      .channel_config = ChannelConfig::CONFIG_STEREO,
      .interleaving_style = InterleavingStyle::PER_CHANNEL,
      .number_of_channels = 2,
      .valid_bit_depth = 16,
      .sample_type = SampleType::INT_MSB,
      .reserved = 0,
  };

  constexpr uint8_t kBusId = 0;
  uint32_t i2s_gateway_id = I2S_GATEWAY_CFG_NODE_ID(DMA_TYPE_I2S_LINK_OUTPUT, kBusId, 0);
  // Use same format for input and output in this test.
  CopierCfg i2s_out_copier = CreateGatewayCopierCfg(format, format, i2s_gateway_id);
  zx::result<std::vector<uint8_t>> i2s_config = GetModuleConfig(
      *nhlt.value().get(), kBusId, NHLT_DIRECTION_RENDER, NHLT_LINK_TYPE_SSP, i2s_out_copier);
  ASSERT_TRUE(i2s_config.is_ok());

  auto* copier = reinterpret_cast<const CopierCfg*>(i2s_config.value().data());
  // Input format.
  EXPECT_EQ(copier->base_cfg.audio_fmt.number_of_channels, 2);
  EXPECT_EQ(copier->base_cfg.audio_fmt.bit_depth, BitDepth::DEPTH_16BIT);
  EXPECT_EQ(copier->base_cfg.audio_fmt.valid_bit_depth, 16);
  // Output format.
  EXPECT_EQ(copier->out_fmt.number_of_channels, 2);
  EXPECT_EQ(copier->out_fmt.bit_depth, BitDepth::DEPTH_16BIT);
  EXPECT_EQ(copier->out_fmt.valid_bit_depth, 16);
  // CopierGatewayCfg.
  EXPECT_EQ(copier->gtw_cfg.node_id, 0xC00);
  EXPECT_EQ(copier->gtw_cfg.dma_buffer_size, 0x180);
  EXPECT_EQ(copier->gtw_cfg.config_words, 0x19);
  // clang-format off
  constexpr uint8_t expected_config[] = {
    0x00, 0x00, 0x00, 0x00, 0x10, 0xFF, 0xFF, 0xFF, 0x10, 0x32, 0xFF, 0xFF, 0x54, 0x76, 0xFF, 0xFF,
    0x46, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x01, 0xC0, 0x87, 0x00, 0x00, 0x70, 0xC0, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x01, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x02, 0x40, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x07, 0x07, 0x20, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0xFF, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00,
    0x7D, 0x00, 0x00, 0x00};
  // clang-format on.
  EXPECT_EQ(sizeof(expected_config), i2s_config.value().size() - sizeof(CopierCfg));
  EXPECT_BYTES_EQ(i2s_config.value().data() + sizeof(CopierCfg) - 4, expected_config,
                  sizeof(expected_config));
  uint32_t expected_zero_word = 0;
  EXPECT_BYTES_EQ(i2s_config.value().data() + sizeof(CopierCfg) - 4 + sizeof(expected_config),
                  &expected_zero_word, sizeof(expected_zero_word));
}

}  // namespace
}  // namespace audio::intel_hda
