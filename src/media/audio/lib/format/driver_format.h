// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_FORMAT_DRIVER_FORMAT_H_
#define SRC_MEDIA_AUDIO_LIB_FORMAT_DRIVER_FORMAT_H_

#include <fuchsia/hardware/audio/cpp/fidl.h>
#include <fuchsia/media/cpp/fidl.h>
#include <zircon/device/audio.h>

namespace media::audio {

struct DriverSampleFormat {
  fuchsia::hardware::audio::SampleFormat sample_format;
  uint8_t bytes_per_sample;
  uint8_t valid_bits_per_sample;
};

// Convert an AudioSampleFormat into an audio stream driver sample_format.
// Returns true if the conversion succeed, or false if it does not.
bool AudioSampleFormatToDriverSampleFormat(fuchsia::media::AudioSampleFormat sample_format,
                                           DriverSampleFormat* driver_sample_format_out);
bool AudioSampleFormatToDriverSampleFormat(fuchsia::media::AudioSampleFormat sample_format,
                                           audio_sample_format_t* driver_sample_format_out);

// Convert an audio stream driver sample_format into an AudioSampleFormat.
// Returns true if the conversion succeed, or false if it does not.
bool DriverSampleFormatToAudioSampleFormat(DriverSampleFormat driver_sample_format,
                                           fuchsia::media::AudioSampleFormat* sample_format_out);
bool DriverSampleFormatToAudioSampleFormat(audio_sample_format_t driver_sample_format,
                                           fuchsia::media::AudioSampleFormat* sample_format_out);

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_LIB_FORMAT_DRIVER_FORMAT_H_
