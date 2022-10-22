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
  kA5 = 3,
};

enum class AmlAudioBlock : uint32_t {
  TDMIN_A,
  TDMIN_B,
  TDMIN_C,
  TDMOUT_A,
  TDMOUT_B,
  TDMOUT_C,
  PDMIN,
  PDMIN_VAD,
};

enum class AmlBus : uint32_t {
  TDM_A = 1,
  TDM_B = 2,
  TDM_C = 3,
};

enum class AmlTdmclk : uint32_t {
  CLK_A = 0,
  CLK_B = 1,
  CLK_C = 2,
  CLK_D = 3,
  CLK_E = 4,
  CLK_F = 5,
};

enum class AmlTdmMclkPad : uint32_t {
  MCLK_PAD_0 = 0,
  MCLK_PAD_1 = 1,
  MCLK_PAD_2 = 2,
};

enum class AmlTdmSclkPad : uint32_t {
  SCLK_PAD_0 = 0,
  SCLK_PAD_1 = 1,
  SCLK_PAD_2 = 2,
};

enum class AmlTdmDatPad : uint32_t {
  TDM_D4 = 4,
  TDM_D5 = 5,
  TDM_D8 = 8,
  TDM_D9 = 9,
  TDM_D10 = 10,
  TDM_D11 = 11,
};

struct AmlLoopbackConfig {
  // If Enable Loopback, select source for TDMIN_LB.
  // e.g.
  //  tdminlb_src = TDMOUT_B.
  //
  // [Data Flow as follow]:
  // `==>` : Play data flow.
  // `-->` : Loopback data flow.
  //
  //                                                        |  (To Codec or BT)
  //                                                        |
  // +--------+     +-------+     +--------+                |   +----------+
  // | player | ==> |FRDDR_*| ==> |TDMOUT_B| ========0======+=> |PAD to Pin|
  // +--------+     +-------+     +--------+         |      |   +----------+
  //                                        (reflow) |      |
  //                                                 |      |
  //                                                 |      |
  //                                        datalb   v      |
  // +--------+     +-------+     +--------+     +--------+ |
  // | record | <-- |TODDR_*| <-- |LOOPBACK| <-- |TDMIN_LB| |
  // +--------+     +-------+     +--------+     +--------+ |
  //                                  ^                     |
  //                                  |       +---------+   |
  //                                   -------|PDM/TDMIN|
  //                                datain    +---------+
  //
  AmlAudioBlock datain_src;
  uint8_t datain_chnum;
  uint32_t datain_chmask;

  AmlAudioBlock datalb_src;
  uint8_t datalb_chnum;
  uint32_t datalb_chmask;
};

struct AmlConfig {
  char manufacturer[kMaxAmlConfigString];
  char product_name[kMaxAmlConfigString];
  bool is_input;

  bool is_loopback;
  AmlLoopbackConfig loopback;

  // If false, it will use HIFI_PLL by default.
  // If true, it will use MP0_PLL
  bool is_custom_tdm_src_clk_sel;

  // If false, it will use same suffix channel by default.
  // e.g.
  //  TDMOUT_A -> MCLK_A -> SCLK_A -> LRCLK_A
  //  TDMOUT_B -> MCLK_B -> SCLK_B -> LRCLK_B
  //  TDMOUT_C -> MCLK_C -> SCLK_C -> LRCLK_C
  // If true, select the channel you want.
  // e.g.
  //  TDMOUT_A -> MCLK_C -> SCLK_C -> LRCLK_C
  //
  bool is_custom_tdm_clk_sel;
  AmlTdmclk tdm_clk_sel;
  // If false, it will use MCLK_PAD_0 by default.
  //  TDMOUT_A/B/C -> MCLK_PAD_0
  // If true, according to board layout design (which pin you used?)
  // then select the right mclk_pad.
  // e.g.
  //  TDMOUT_A -> MCLK_PAD_2
  //
  bool is_custom_tdm_mpad_sel;
  AmlTdmMclkPad mpad_sel;
  // If false, it will use same suffix channel by default.
  //  TDMOUT_A -> SCLK_PAD_0 -> LRCLK_PAD_0
  //  TDMOUT_B -> SCLK_PAD_1 -> LRCLK_PAD_1
  //  TDMOUT_C -> SCLK_PAD_2 -> LRCLK_PAD_2
  // If true, according to board layout design (which pins you used?)
  // then select the right sclk_pad, lrclk_pad.
  // e.g.
  //  TDMOUT_A -> SCLK_PAD_2, LRCLK_PAD_2
  //
  bool is_custom_tdm_spad_sel;
  AmlTdmSclkPad spad_sel;
  // dpad_mask: support 8x data lane out select.
  // bit[7:0] : lane0 ~ lane7.
  // each lane can choose one of tmd_out(32 channel).
  // e.g. use 4 lane (tdmoutb)
  // Note: tdm_d2/d3  -> pin function
  //
  //  -     / LANE_0 -> tdm_d2 -> GPIOC_0 -> codec sdin_0
  // |d|   /  LANE_1 -> tdm_d3 -> GPIOC_1 -> codec sdin_1
  // |a| =>
  // |t|   \  LANE_2 -> tdm_d4 -> GPIOC_5 -> codec sdin_2
  // |a|    \ LANE_3 -> tdm_d5 -> GPIOC_6 -> codec sdin_3
  //  -
  //
  uint8_t dpad_mask;
  AmlTdmDatPad dpad_sel[kMaxNumberOfLanes];
  uint32_t mClockDivFactor;
  uint32_t sClockDivFactor;
  audio_stream_unique_id_t unique_id;
  uint32_t swaps;  // Configures routing, one channel per nibble.
  // Lanes is a AMLogic specific concept allowing routing to different input/outputs, for instance
  // 2 lanes can be used to send audio to 2 different DAI interfaces. What bits are enabled in
  // lanes_enable_mask defines what is read/write from/to the ring buffer and routed to each lane.
  uint32_t lanes_enable_mask[kMaxNumberOfLanes];
  AmlBus bus;
  AmlVersion version;
  RingBuffer ring_buffer;
  Dai dai;
  Codecs codecs;
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
