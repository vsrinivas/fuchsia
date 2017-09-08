// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <audio-proto-utils/format-utils.h>
#include <map>

#include "garnet/bin/media/audio/driver_utils.h"

namespace media {
namespace driver_utils {

namespace {
static constexpr audio_sample_format_t AUDIO_SAMPLE_FORMAT_UNSIGNED_8BIT =
  static_cast<audio_sample_format_t>(AUDIO_SAMPLE_FORMAT_8BIT |
                                     AUDIO_SAMPLE_FORMAT_FLAG_UNSIGNED);

namespace AST {
using SampleFormat = AudioStreamType::SampleFormat;
}

static const std::map<audio_sample_format_t, AST::SampleFormat>
  kDriverSampleFormatToSampleFormatMap = {
  { AUDIO_SAMPLE_FORMAT_UNSIGNED_8BIT, AST::SampleFormat::kUnsigned8 },
  { AUDIO_SAMPLE_FORMAT_16BIT,         AST::SampleFormat::kSigned16 },
  { AUDIO_SAMPLE_FORMAT_24BIT_IN32,    AST::SampleFormat::kSigned24In32 },
  { AUDIO_SAMPLE_FORMAT_32BIT_FLOAT,   AST::SampleFormat::kFloat },
};

static const std::map<AST::SampleFormat, audio_sample_format_t>
  kSampleFormatToDriverSampleFormatMap = {
  { AST::SampleFormat::kUnsigned8,    AUDIO_SAMPLE_FORMAT_UNSIGNED_8BIT },
  { AST::SampleFormat::kSigned16,     AUDIO_SAMPLE_FORMAT_16BIT },
  { AST::SampleFormat::kSigned24In32, AUDIO_SAMPLE_FORMAT_24BIT_IN32 },
  { AST::SampleFormat::kFloat,        AUDIO_SAMPLE_FORMAT_32BIT_FLOAT },
};

}  // anon namespace

bool SampleFormatToDriverSampleFormat(
    AudioStreamType::SampleFormat sample_format,
    audio_sample_format_t* driver_sample_format_out) {
  FTL_DCHECK(driver_sample_format_out != nullptr);

  auto iter = kSampleFormatToDriverSampleFormatMap.find(sample_format);
  if (iter == kSampleFormatToDriverSampleFormatMap.end()) {
    return false;
  }

  *driver_sample_format_out = iter->second;
  return true;
}

bool DriverSampleFormatToSampleFormat(
    audio_sample_format_t driver_sample_format,
    AudioStreamType::SampleFormat* sample_format_out) {

  auto iter = kDriverSampleFormatToSampleFormatMap.find(driver_sample_format);
  if (iter == kDriverSampleFormatToSampleFormatMap.end()) {
    return false;
  }

  *sample_format_out = iter->second;
  return true;
}

void AddAudioStreamTypeSets(
    audio_stream_format_range_t fmt,
    std::vector<std::unique_ptr<media::StreamTypeSet>>* typeset_target) {
  FTL_DCHECK(typeset_target != nullptr);

  uint32_t flags = fmt.sample_formats & AUDIO_SAMPLE_FORMAT_FLAG_MASK;
  uint32_t noflags = fmt.sample_formats & ~AUDIO_SAMPLE_FORMAT_FLAG_MASK;

  for (uint32_t i = 1; noflags != 0; i <<= 1) {
    if (!(i & noflags))
      continue;

    noflags &= ~i;

    AudioStreamType::SampleFormat sample_format;
    auto driver_sample_format = static_cast<audio_sample_format_t>(i | flags);
    if (!DriverSampleFormatToSampleFormat(driver_sample_format,
                                          &sample_format)) {
      FTL_LOG(WARNING) << "Failed to map driver sample format 0x"
                       << std::hex << driver_sample_format
                       << " to AudioStreamType::SampleFormat.  Skipping.";
      continue;
    }

    if (fmt.flags & ASF_RANGE_FLAG_FPS_CONTINUOUS) {
      typeset_target->push_back(AudioStreamTypeSet::Create(
          { AudioStreamType::kAudioEncodingLpcm },
          sample_format,
          Range<uint32_t>(fmt.min_channels, fmt.max_channels),
          Range<uint32_t>(fmt.min_frames_per_second,
                          fmt.max_frames_per_second)));
    } else {
      audio::utils::FrameRateEnumerator enumerator(fmt);
      for (uint32_t rate : enumerator) {
        typeset_target->push_back(AudioStreamTypeSet::Create(
            { AudioStreamType::kAudioEncodingLpcm },
            sample_format,
            Range<uint32_t>(fmt.min_channels, fmt.max_channels),
            Range<uint32_t>(rate, rate)));
      }
    }
  }
}


}  // namespace driver_utils
}  // namespace media

