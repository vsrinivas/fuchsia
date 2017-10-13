// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/media/audio_output_device.h"
#include "garnet/lib/media/audio_output_manager.h"
#include "garnet/lib/media/audio_output_stream.h"

namespace media_client {

AudioOutputDevice::AudioOutputDevice(const char* device_id,
                                     const char* device_name,
                                     int preferred_sample_rate,
                                     int preferred_num_channels,
                                     int preferred_num_frames_in_buffer,
                                     zx_duration_t min_delay_nsec)
    : preferred_sample_rate_(preferred_sample_rate),
      preferred_num_channels_(preferred_num_channels),
      preferred_num_frames_in_buffer_(preferred_num_frames_in_buffer),
      min_delay_nsec_(min_delay_nsec) {
  id_ = device_id;
  name_ = device_name;
}
AudioOutputDevice::~AudioOutputDevice() {
  for (const auto& stream : streams_)
    stream->Stop();
}

AudioOutputStream* AudioOutputDevice::CreateStream(
    fuchsia_audio_parameters* stream_params) {
  auto stream = std::make_unique<AudioOutputStream>();

  AudioOutputStream* stream_ptr = nullptr;

  if (stream->Initialize(stream_params, min_delay_nsec_, this)) {
    stream_ptr = stream.get();
    streams_.push_back(std::move(stream));
  }

  return stream_ptr;
}

int AudioOutputDevice::FreeStream(AudioOutputStream* stream) {
  stream->Stop();

  streams_.erase(
      std::find_if(streams_.begin(), streams_.end(),
                   [stream](const auto& s) { return s.get() == stream; }));

  return ZX_OK;
}

}  // namespace media_client
