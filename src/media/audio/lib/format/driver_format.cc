// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/format/driver_format.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <map>

#include <audio-proto-utils/format-utils.h>

namespace media::audio {

namespace {

static const std::map<fuchsia::media::AudioSampleFormat, DriverSampleFormat>
    kSampleFormatToDriver2SampleFormatMap = {
        {fuchsia::media::AudioSampleFormat::UNSIGNED_8,
         {fuchsia::hardware::audio::SampleFormat::PCM_UNSIGNED, 1, 8}},
        {fuchsia::media::AudioSampleFormat::SIGNED_16,
         {fuchsia::hardware::audio::SampleFormat::PCM_SIGNED, 2, 16}},
        {fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32,
         {fuchsia::hardware::audio::SampleFormat::PCM_SIGNED, 4, 24}},
        {fuchsia::media::AudioSampleFormat::FLOAT,
         {fuchsia::hardware::audio::SampleFormat::PCM_FLOAT, 4, 32}},
};

static constexpr audio_sample_format_t AUDIO_SAMPLE_FORMAT_UNSIGNED_8BIT =
    static_cast<audio_sample_format_t>(AUDIO_SAMPLE_FORMAT_8BIT |
                                       AUDIO_SAMPLE_FORMAT_FLAG_UNSIGNED);

static const std::map<audio_sample_format_t, fuchsia::media::AudioSampleFormat>
    kDriverSampleFormatToSampleFormatMap = {
        {AUDIO_SAMPLE_FORMAT_UNSIGNED_8BIT, fuchsia::media::AudioSampleFormat::UNSIGNED_8},
        {AUDIO_SAMPLE_FORMAT_16BIT, fuchsia::media::AudioSampleFormat::SIGNED_16},
        {AUDIO_SAMPLE_FORMAT_24BIT_IN32, fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32},
        {AUDIO_SAMPLE_FORMAT_32BIT_FLOAT, fuchsia::media::AudioSampleFormat::FLOAT},
};

static const std::map<fuchsia::media::AudioSampleFormat, audio_sample_format_t>
    kSampleFormatToDriverSampleFormatMap = {
        {fuchsia::media::AudioSampleFormat::UNSIGNED_8, AUDIO_SAMPLE_FORMAT_UNSIGNED_8BIT},
        {fuchsia::media::AudioSampleFormat::SIGNED_16, AUDIO_SAMPLE_FORMAT_16BIT},
        {fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32, AUDIO_SAMPLE_FORMAT_24BIT_IN32},
        {fuchsia::media::AudioSampleFormat::FLOAT, AUDIO_SAMPLE_FORMAT_32BIT_FLOAT},
};

}  // namespace

bool AudioSampleFormatToDriverSampleFormat(fuchsia::media::AudioSampleFormat sample_format,
                                           audio_sample_format_t* driver_sample_format_out) {
  TRACE_DURATION("audio", "AudioSampleFormatToDriverSampleFormat");
  FX_DCHECK(driver_sample_format_out != nullptr);

  auto iter = kSampleFormatToDriverSampleFormatMap.find(sample_format);
  if (iter == kSampleFormatToDriverSampleFormatMap.end()) {
    return false;
  }

  *driver_sample_format_out = iter->second;
  return true;
}

bool DriverSampleFormatToAudioSampleFormat(audio_sample_format_t driver_sample_format,
                                           fuchsia::media::AudioSampleFormat* sample_format_out) {
  TRACE_DURATION("audio", "DriverSampleFormatToAudioSampleFormat");
  auto iter = kDriverSampleFormatToSampleFormatMap.find(driver_sample_format);
  if (iter == kDriverSampleFormatToSampleFormatMap.end()) {
    return false;
  }

  *sample_format_out = iter->second;
  return true;
}

bool AudioSampleFormatToDriverSampleFormat(fuchsia::media::AudioSampleFormat sample_format,
                                           DriverSampleFormat* driver_sample_format_out) {
  TRACE_DURATION("audio", "AudioSampleFormatToDriverSampleFormat");
  FX_DCHECK(driver_sample_format_out != nullptr);

  auto iter = kSampleFormatToDriver2SampleFormatMap.find(sample_format);
  if (iter == kSampleFormatToDriver2SampleFormatMap.end()) {
    return false;
  }
  *driver_sample_format_out = iter->second;
  return true;
}

bool DriverSampleFormatToAudioSampleFormat(DriverSampleFormat driver_sample_format,
                                           fuchsia::media::AudioSampleFormat* sample_format_out) {
  TRACE_DURATION("audio", "DriverSampleFormatToAudioSampleFormat");
  for (auto& i : kSampleFormatToDriver2SampleFormatMap) {
    if (i.second.sample_format == driver_sample_format.sample_format &&
        i.second.bytes_per_sample == driver_sample_format.bytes_per_sample &&
        i.second.valid_bits_per_sample == driver_sample_format.valid_bits_per_sample) {
      *sample_format_out = i.first;
      return true;
    }
  }
  return false;
}

}  // namespace media::audio
