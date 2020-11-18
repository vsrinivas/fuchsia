// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_LIB_DDKTL_INCLUDE_DDKTL_METADATA_AUDIO_H_
#define SRC_LIB_DDKTL_INCLUDE_DDKTL_METADATA_AUDIO_H_

#include <stdint.h>

namespace metadata {

static constexpr uint32_t kMaxNumberOfCodecs = 8;
static constexpr uint32_t kMaxNumberOfExternalDelays = 8;

enum class CodecType : uint32_t {
  Tas27xx,
  Tas5782,
  Tas58xx,
  Tas5720,
};

enum class DaiType : uint32_t {
  I2s,
  StereoLeftJustified,
  Tdm1,
};

enum class SampleFormat : uint32_t {
  PcmSigned,  // Default for zeroed out metadata.
  PcmUnsigned,
  PcmFloat,
};

struct ExternalDelay {
  uint32_t frequency;
  int64_t nsecs;
};

struct RingBuffer {
  uint8_t number_of_channels;
  uint8_t bytes_per_sample;  // If not specified (set to 0), then 2 bytes.
};

struct Dai {
  DaiType type;
  uint8_t number_of_channels;  // If not specified (set to 0), then 2 for stereo types like I2S.
  SampleFormat sample_format;  // Defaults to PcmSigned.
  uint8_t bits_per_sample;     // If not specified (set to 0), then 16 bits.
  uint8_t bits_per_slot;       // If not specified (set to 0), then 32 bits.
  bool sclk_on_raising;        // Invert the usual clocking out on falling edge.
};

struct Codecs {
  uint8_t number_of_codecs;
  CodecType types[kMaxNumberOfCodecs];
  float delta_gains[kMaxNumberOfCodecs];
  uint32_t number_of_external_delays;
  ExternalDelay external_delays[kMaxNumberOfExternalDelays];
  // Channel to enable in each codec.
  uint8_t channels_to_use_bitmask[kMaxNumberOfCodecs];
};

}  // namespace metadata

#endif  // SRC_LIB_DDKTL_INCLUDE_DDKTL_METADATA_AUDIO_H_
