// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

namespace media {

// Enumerates USB audio inputs and outputs.
class UsbAudioEnum {
 public:
  UsbAudioEnum();

  ~UsbAudioEnum();

  const std::vector<std::string>& input_device_paths() {
    return input_device_paths_;
  }

  const std::vector<std::string>& output_device_paths() {
    return output_device_paths_;
  }

 private:
  static const std::string kAudioDeviceClassPath;

  std::vector<std::string> input_device_paths_;
  std::vector<std::string> output_device_paths_;
};

}  // namespace media
