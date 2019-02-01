// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_CORE_DRIVER_UTILS_H_
#define GARNET_BIN_MEDIA_AUDIO_CORE_DRIVER_UTILS_H_

#include <fuchsia/media/cpp/fidl.h>
#include <zircon/device/audio.h>

namespace media {
namespace driver_utils {

// Convert an AudioSampleFormat into an audio stream driver sample_format.
// Returns true if the conversion succeed, or false if it does not.
bool AudioSampleFormatToDriverSampleFormat(
    fuchsia::media::AudioSampleFormat sample_format,
    audio_sample_format_t* driver_sample_format_out);

// Convert an audio stream driver sample_format into an AudioSampleFormat.
// Returns true if the conversion succeed, or false if it does not.
bool DriverSampleFormatToAudioSampleFormat(
    audio_sample_format_t driver_sample_format,
    fuchsia::media::AudioSampleFormat* sample_format_out);

}  // namespace driver_utils
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_CORE_DRIVER_UTILS_H_
