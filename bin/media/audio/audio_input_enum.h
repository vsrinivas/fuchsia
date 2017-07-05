// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

namespace media {

// Enumerates audio inputs.
class AudioInputEnum {
 public:
  AudioInputEnum();

  ~AudioInputEnum();

  const std::vector<std::string>& input_device_paths() {
    return input_device_paths_;
  }

 private:
  static const std::string kAudioInputDeviceClassPath;
  std::vector<std::string> input_device_paths_;
};

}  // namespace media
