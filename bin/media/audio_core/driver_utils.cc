// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>

#include <audio-proto-utils/format-utils.h>

#include "garnet/bin/media/audio_core/driver_utils.h"
#include "lib/fxl/logging.h"

namespace media {
namespace driver_utils {

namespace {
static constexpr audio_sample_format_t AUDIO_SAMPLE_FORMAT_UNSIGNED_8BIT =
    static_cast<audio_sample_format_t>(AUDIO_SAMPLE_FORMAT_8BIT |
                                       AUDIO_SAMPLE_FORMAT_FLAG_UNSIGNED);

static const std::map<audio_sample_format_t, fuchsia::media::AudioSampleFormat>
    kDriverSampleFormatToSampleFormatMap = {
        {AUDIO_SAMPLE_FORMAT_UNSIGNED_8BIT,
         fuchsia::media::AudioSampleFormat::UNSIGNED_8},
        {AUDIO_SAMPLE_FORMAT_16BIT,
         fuchsia::media::AudioSampleFormat::SIGNED_16},
        {AUDIO_SAMPLE_FORMAT_24BIT_IN32,
         fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32},
        {AUDIO_SAMPLE_FORMAT_32BIT_FLOAT,
         fuchsia::media::AudioSampleFormat::FLOAT},
};

static const std::map<fuchsia::media::AudioSampleFormat, audio_sample_format_t>
    kSampleFormatToDriverSampleFormatMap = {
        {fuchsia::media::AudioSampleFormat::UNSIGNED_8,
         AUDIO_SAMPLE_FORMAT_UNSIGNED_8BIT},
        {fuchsia::media::AudioSampleFormat::SIGNED_16,
         AUDIO_SAMPLE_FORMAT_16BIT},
        {fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32,
         AUDIO_SAMPLE_FORMAT_24BIT_IN32},
        {fuchsia::media::AudioSampleFormat::FLOAT,
         AUDIO_SAMPLE_FORMAT_32BIT_FLOAT},
};

}  // namespace

bool AudioSampleFormatToDriverSampleFormat(
    fuchsia::media::AudioSampleFormat sample_format,
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
    fuchsia::media::AudioSampleFormat* sample_format_out) {
  auto iter = kDriverSampleFormatToSampleFormatMap.find(driver_sample_format);
  if (iter == kDriverSampleFormatToSampleFormatMap.end()) {
    return false;
  }

  *sample_format_out = iter->second;
  return true;
}

}  // namespace driver_utils
}  // namespace media
