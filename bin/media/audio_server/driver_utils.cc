// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <audio-proto-utils/format-utils.h>
#include <map>

#include "garnet/bin/media/audio_server/driver_utils.h"
#include "lib/fxl/logging.h"

namespace media {
namespace driver_utils {

namespace {
static constexpr audio_sample_format_t AUDIO_SAMPLE_FORMAT_UNSIGNED_8BIT =
    static_cast<audio_sample_format_t>(AUDIO_SAMPLE_FORMAT_8BIT |
                                       AUDIO_SAMPLE_FORMAT_FLAG_UNSIGNED);

static const std::map<audio_sample_format_t, AudioSampleFormat>
    kDriverSampleFormatToSampleFormatMap = {
        {AUDIO_SAMPLE_FORMAT_UNSIGNED_8BIT, AudioSampleFormat::UNSIGNED_8},
        {AUDIO_SAMPLE_FORMAT_16BIT, AudioSampleFormat::SIGNED_16},
        {AUDIO_SAMPLE_FORMAT_24BIT_IN32, AudioSampleFormat::SIGNED_24_IN_32},
        {AUDIO_SAMPLE_FORMAT_32BIT_FLOAT, AudioSampleFormat::FLOAT},
};

static const std::map<AudioSampleFormat, audio_sample_format_t>
    kSampleFormatToDriverSampleFormatMap = {
        {AudioSampleFormat::UNSIGNED_8, AUDIO_SAMPLE_FORMAT_UNSIGNED_8BIT},
        {AudioSampleFormat::SIGNED_16, AUDIO_SAMPLE_FORMAT_16BIT},
        {AudioSampleFormat::SIGNED_24_IN_32, AUDIO_SAMPLE_FORMAT_24BIT_IN32},
        {AudioSampleFormat::FLOAT, AUDIO_SAMPLE_FORMAT_32BIT_FLOAT},
};

}  // namespace

bool AudioSampleFormatToDriverSampleFormat(
    AudioSampleFormat sample_format,
    audio_sample_format_t* driver_sample_format_out) {
  FXL_DCHECK(driver_sample_format_out != nullptr);

  auto iter = kSampleFormatToDriverSampleFormatMap.find(sample_format);
  if (iter == kSampleFormatToDriverSampleFormatMap.end()) {
    return false;
  }

  *driver_sample_format_out = iter->second;
  return true;
}

bool DriverSampleFormatToAudioSampleFormat(
    audio_sample_format_t driver_sample_format,
    AudioSampleFormat* sample_format_out) {
  auto iter = kDriverSampleFormatToSampleFormatMap.find(driver_sample_format);
  if (iter == kDriverSampleFormatToSampleFormatMap.end()) {
    return false;
  }

  *sample_format_out = iter->second;
  return true;
}

}  // namespace driver_utils
}  // namespace media
