// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <zircon/types.h>

#include "lib/media/c/audio.h"

namespace media_client {

class AudioOutputStream;
class AudioOutputManager;

class AudioOutputDevice {
 public:
  AudioOutputDevice(const char* device_id,
                    const char* device_name,
                    int preferred_sample_rate,
                    int preferred_num_channels,
                    int preferred_num_frames_in_buffer,
                    zx_duration_t min_delay_nsec);
  ~AudioOutputDevice();

  const std::string& id() const { return id_; }
  const std::string& name() const { return name_; }

  int preferred_sample_rate() const { return preferred_sample_rate_; }
  int preferred_num_channels() const { return preferred_num_channels_; }
  int preferred_buffer_size() const { return preferred_num_frames_in_buffer_; }

  AudioOutputStream* CreateStream(fuchsia_audio_parameters* parameters);
  int FreeStream(AudioOutputStream* stream);

 private:
  std::vector<std::unique_ptr<AudioOutputStream>> streams_;

  std::string id_;
  std::string name_;

  int preferred_sample_rate_;
  int preferred_num_channels_;
  int preferred_num_frames_in_buffer_;
  zx_duration_t min_delay_nsec_;
};

}  // namespace media_client
