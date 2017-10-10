// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>
#include <zircon/types.h>

namespace media {

// Enumerates audio inputs.
class AudioInputEnum {
 public:
  struct Details {
    Details(std::string p, zx_time_t pt) : path(std::move(p)), plug_time(pt) {}

    std::string path;
    zx_time_t plug_time;
  };

  AudioInputEnum();
  ~AudioInputEnum();

  const std::vector<const Details>& input_devices() {
    return input_devices_;
  }

 private:
  static const std::string kAudioInputDeviceClassPath;
  std::vector<const Details> input_devices_;
};

}  // namespace media
