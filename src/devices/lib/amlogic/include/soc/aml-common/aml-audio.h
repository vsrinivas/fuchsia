// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_AUDIO_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_AUDIO_H_

#include <zircon/device/audio.h>

#include <ddktl/metadata/audio.h>

namespace metadata {

static constexpr uint32_t kMaxNumberOfLanes = 4;
static constexpr uint32_t kMaxAmlConfigString = 32;

enum class AmlVersion : uint32_t {
  kS905D2G = 1,  // Also works with T931G.
  kS905D3G = 2,
};

enum class AmlBus : uint32_t {
  TDM_A = 1,
  TDM_B = 2,
  TDM_C = 3,
};

struct AmlConfig {
  char manufacturer[kMaxAmlConfigString];
  char product_name[kMaxAmlConfigString];
  bool is_input;
  uint32_t mClockDivFactor;
  uint32_t sClockDivFactor;
  audio_stream_unique_id_t unique_id;
  uint8_t number_of_channels;  // Total number of channels in the ring buffer.
  // How many channels in the DAI, we only support one per AmlConfig (and hence driver) instance.
  // Not needed for stereo TDM types like I2S (implicitly 2).
  uint32_t dai_number_of_channels;
  uint32_t swaps;  // Configures routing, one channel per nibble.
  // Lanes is a AMLogic specific concept allowing routing to different input/outputs, for instance
  // 2 lanes can be used to send audio to 2 different DAI interfaces. What bits are enabled in
  // lanes_enable_mask defines what is read/write from/to the ring buffer and routed to each lane.
  uint32_t lanes_enable_mask[kMaxNumberOfLanes];
  AmlBus bus;
  AmlVersion version;
  Tdm tdm;
  // Channel to enable in each codec.
  uint8_t codecs_channels_mask[metadata::kMaxNumberOfCodecs];
  // Configures L+R mixing, one bit per channel pair.
  uint8_t mix_mask;
};

struct AmlPdmConfig {
  char manufacturer[kMaxAmlConfigString];
  char product_name[kMaxAmlConfigString];
  uint8_t number_of_channels;  // Total number of channels in the ring buffer.
  AmlVersion version;
  uint32_t sysClockDivFactor;
  uint32_t dClockDivFactor;
};

}  // namespace metadata

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_AUDIO_H_
