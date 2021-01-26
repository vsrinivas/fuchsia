// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AUDIO_AUDIO_H_
#define SRC_DEVICES_LIB_AUDIO_AUDIO_H_

#include <fuchsia/hardware/audiotypes/c/banjo.h>
#include <zircon/device/audio.h>

namespace audio {

void audio_stream_format_fidl_from_banjo(const audio_types_audio_stream_format_range_t& source,
                                         audio_stream_format_range* destination) {
  destination->sample_formats = source.sample_formats;
  destination->min_frames_per_second = source.min_frames_per_second;
  destination->max_frames_per_second = source.max_frames_per_second;
  destination->min_channels = source.min_channels;
  destination->max_channels = source.max_channels;
  destination->flags = source.flags;
}

}  // namespace audio

#endif  // SRC_DEVICES_LIB_AUDIO_AUDIO_H_
