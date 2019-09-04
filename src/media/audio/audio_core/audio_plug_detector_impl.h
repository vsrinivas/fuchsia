// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_PLUG_DETECTOR_IMPL_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_PLUG_DETECTOR_IMPL_H_

#include <lib/fsl/io/device_watcher.h>

#include <memory>
#include <vector>

#include "src/media/audio/audio_core/audio_plug_detector.h"

namespace media::audio {

class AudioPlugDetectorImpl : public AudioPlugDetector {
 public:
  zx_status_t Start(Observer o) final;
  void Stop() final;

 private:
  void AddAudioDevice(int dir_fd, const std::string& name, bool is_input);

  Observer observer_;
  std::vector<std::unique_ptr<fsl::DeviceWatcher>> watchers_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_PLUG_DETECTOR_IMPL_H_
