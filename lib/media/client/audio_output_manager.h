// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "lib/media/c/audio.h"

namespace media_client {

class AudioOutputDevice;
class AudioOutputStream;

class AudioOutputManager {
 public:
  AudioOutputManager();
  ~AudioOutputManager();

  int GetOutputDevices(fuchsia_audio_device_description* buffer,
                       int num_device_descriptions);

  int GetOutputDeviceDefaultParameters(char* device_id,
                                       fuchsia_audio_parameters* stream_params);

  int CreateOutputStream(char* device_id,
                         fuchsia_audio_parameters* stream_params,
                         media_client::AudioOutputStream** stream_out);

 private:
  void EnumerateDevices();

  int GetDeviceNumFromId(char* device_id);

  int num_devices_;
  std::vector<std::unique_ptr<media_client::AudioOutputDevice>> devices_;
};

}  // namespace media_client
