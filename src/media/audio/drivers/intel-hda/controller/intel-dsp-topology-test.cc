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
  StatusOr<std::unique_ptr<Nhlt>> nhlt = Nhlt::FromBuffer(kExampleNhtl);
  ASSERT_TRUE(nhlt.ok());

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
  ASSERT_OK(GetNhltBlob(*nhlt.ValueOrDie().get(), kBusId, NHLT_DIRECTION_RENDER, NHLT_LINK_TYPE_SSP,
                        format, &data, &data_size));
}

TEST(DspTopology, GetPdmBlob) {
  StatusOr<std::unique_ptr<Nhlt>> nhlt = Nhlt::FromBuffer(kExampleNhtl);
  ASSERT_TRUE(nhlt.ok());

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
  ASSERT_OK(GetNhltBlob(*nhlt.ValueOrDie().get(), kBusId, NHLT_DIRECTION_CAPTURE,
                        NHLT_LINK_TYPE_PDM, format, &data, &data_size));
}

}  // namespace
}  // namespace audio::intel_hda
