// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_DEVICE_LISTER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_DEVICE_LISTER_H_

#include <fuchsia/media/cpp/fidl.h>

#include <vector>

namespace media::audio {

class DeviceLister {
 public:
  virtual ~DeviceLister() = default;

  // Returns set of available devices.
  virtual std::vector<fuchsia::media::AudioDeviceInfo> GetDeviceInfos() = 0;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_DEVICE_LISTER_H_
