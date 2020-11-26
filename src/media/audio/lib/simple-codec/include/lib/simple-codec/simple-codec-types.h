// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_TYPES_H_
#define SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_TYPES_H_

#include <fuchsia/hardware/audio/cpp/fidl.h>

#include <string>
#include <vector>

namespace audio {

struct DriverIds {
  // Driver vendor id, for instance PDEV_VID_TI.
  uint32_t vendor_id;
  // Driver device id, for instance PDEV_DID_TI_TAS2770.
  uint32_t device_id;
  // If there is more than one of the same codec in the system set to count starting from 1.
  uint32_t instance_count;
};

// The types below have the same meaning as those described via FIDL in
// //sdk/fidl/fuchsia.hardware.audio/codec.fidl,
// //sdk/fidl/fuchsia.hardware.audio/stream.fidl and
// //sdk/fidl/fuchsia.hardware.audio/dai_format.fidl.
using SampleFormat = ::fuchsia::hardware::audio::DaiSampleFormat;
using FrameFormat = ::fuchsia::hardware::audio::DaiFrameFormatStandard;
using GainType = ::fuchsia::hardware::audio::GainType;
using Info = ::fuchsia::hardware::audio::CodecInfo;

struct DaiFormat final {
  uint32_t number_of_channels{};
  uint64_t channels_to_use_bitmask{};
  SampleFormat sample_format{};
  FrameFormat frame_format{};
  uint32_t frame_rate{};
  uint8_t bits_per_slot{};
  uint8_t bits_per_sample{};
};

struct DaiSupportedFormats final {
  std::vector<uint32_t> number_of_channels{};
  std::vector<SampleFormat> sample_formats{};
  std::vector<FrameFormat> frame_formats{};
  std::vector<uint32_t> frame_rates{};
  std::vector<uint8_t> bits_per_slot{};
  std::vector<uint8_t> bits_per_sample{};
};

struct GainFormat {
  float min_gain;
  float max_gain;
  float gain_step;
  bool can_mute;
  bool can_agc;
};

struct GainState {
  float gain;
  bool muted;
  bool agc_enabled;
};

}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_TYPES_H_
