// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_TYPES_H_
#define SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_TYPES_H_

#include <string>
#include <vector>

#include <ddktl/protocol/codec.h>

namespace audio {

struct DriverIds {
  // Driver vendor id, for instance PDEV_VID_TI.
  uint32_t vendor_id;
  // Driver device id, for instance PDEV_DID_TI_TAS2770.
  uint32_t device_id;
  // If there is more than one of the same codec in the system set to count starting from 1.
  uint32_t instance_count;
};

struct Info {
  // Unique identifier for the codec.
  std::string unique_id;
  // Name of the codec manufacturer/vendor.
  std::string manufacturer;
  // Product/device name of the codec.
  std::string product_name;
};

struct DaiSupportedFormats {
  // All possible number of channels supported by the codec.
  std::vector<uint32_t> number_of_channels;
  // Sample formats supported by the codec.
  std::vector<sample_format_t> sample_formats;
  // Frame formats supported by the codec.
  std::vector<frame_format_t> frame_formats;
  // Rates supported by the codec.
  std::vector<uint32_t> frame_rates;
  // The bits per slot supported by the codec.
  std::vector<uint8_t> bits_per_slot;
  // Bits per sample supported by the codec.
  std::vector<uint8_t> bits_per_sample;
};

struct DaiFormat {
  // Number of channels in the DAI.
  uint32_t number_of_channels;
  // Which channels to use in the DAI.
  std::vector<uint32_t> channels_to_use;
  // The format of all samples in the DAI.
  sample_format_t sample_format;
  // The justification of all samples in the DAI.
  frame_format_t frame_format;
  // The frame rate for all samples in the DAI.
  uint32_t frame_rate;
  // The bits per slot for all channels in the DAI.
  uint8_t bits_per_slot;
  // The bits per sample for all samples in the DAI.  Must be smaller than bits per channel for
  // samples to fit.
  uint8_t bits_per_sample;
};

struct GainFormat {
  // Minimum gain that could be set in the codec.
  float min_gain_db;
  // Maximum gain that could be set in the codec.
  float max_gain_db;
  // Smallest increment for gain values starting from min_gain.
  float gain_step_db;
  // Is the codec capable of muting.
  bool can_mute;
  // Is the codec capable of automatic gain control.
  bool can_agc;
};

struct GainState {
  // Gain amount in dB.
  float gain_db;
  // Codec muted state.
  bool muted;
  // Codec AGC enabled state.
  bool agc_enable;
};

struct PlugState {
  // Codec is hardwired.
  bool hardwired;
  // Codec is plugged in.
  bool plugged;
};

}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_TYPES_H_
