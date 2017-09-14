// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/device/audio.h>

#include "garnet/bin/media/framework/types/audio_stream_type.h"

namespace media {
namespace driver_utils {

// Convert a media framework SampleFormat into an audio stream driver
// sample_format.  Returns true if the conversion succeed, or false if it does
// not.
bool SampleFormatToDriverSampleFormat(
    AudioStreamType::SampleFormat sample_format,
    audio_sample_format_t* driver_sample_format_out);

// Convert an audio stream driver sample_format into a media framework
// SampleFormat.  Returns true if the conversion succeed, or false if it does
// not.
bool DriverSampleFormatToSampleFormat(
    audio_sample_format_t driver_sample_format,
    AudioStreamType::SampleFormat* sample_format_out);

// Convert the supplied driver interface audio_stream_format_t into
// AudioStreamTypeSets and add them to the target vector of types.
void AddAudioStreamTypeSets(
    audio_stream_format_range_t fmt,
    std::vector<std::unique_ptr<media::StreamTypeSet>>* typeset_target);

}  // namespace driver_utils
}  // namespace media

