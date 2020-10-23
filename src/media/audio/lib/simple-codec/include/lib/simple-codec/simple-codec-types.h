// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_TYPES_H_
#define SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_TYPES_H_

#include <fuchsia/hardware/audio/codec/cpp/fidl.h>

#include <string>
#include <vector>

namespace audio {

using SampleFormat = ::fuchsia::hardware::audio::codec::SampleFormat;
using FrameFormat = ::fuchsia::hardware::audio::codec::FrameFormat;
using FrameFormatCustom = ::fuchsia::hardware::audio::codec::FrameFormatCustom;
using DaiSupportedFormats = ::fuchsia::hardware::audio::codec::DaiSupportedFormats;
using DaiFormat = ::fuchsia::hardware::audio::codec::DaiFormat;
using GainFormat = ::fuchsia::hardware::audio::codec::GainFormat;
using GainType = ::fuchsia::hardware::audio::codec::GainType;
using GainState = ::fuchsia::hardware::audio::codec::GainState;
using PlugState = ::fuchsia::hardware::audio::codec::PlugState;
using Info = ::fuchsia::hardware::audio::codec::Info;

struct DriverIds {
  // Driver vendor id, for instance PDEV_VID_TI.
  uint32_t vendor_id;
  // Driver device id, for instance PDEV_DID_TI_TAS2770.
  uint32_t device_id;
  // If there is more than one of the same codec in the system set to count starting from 1.
  uint32_t instance_count;
};

}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_TYPES_H_
