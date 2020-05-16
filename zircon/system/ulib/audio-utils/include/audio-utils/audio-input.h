// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef AUDIO_UTILS_AUDIO_INPUT_H_
#define AUDIO_UTILS_AUDIO_INPUT_H_

#include <zircon/types.h>

#include <memory>

#include <audio-utils/audio-device-stream.h>

namespace audio {
namespace utils {

class AudioSink;

class AudioInput : public AudioDeviceStream {
 public:
  static std::unique_ptr<AudioInput> Create(uint32_t dev_id);
  static std::unique_ptr<AudioInput> Create(const char* dev_path);
  zx_status_t Record(AudioSink& sink, Duration duration);

 private:
  friend class std::default_delete<AudioInput>;
  friend class AudioDeviceStream;

  explicit AudioInput(uint32_t dev_id) : AudioDeviceStream(StreamDirection::kInput, dev_id) {}
  explicit AudioInput(const char* dev_path)
      : AudioDeviceStream(StreamDirection::kInput, dev_path) {}
};

}  // namespace utils
}  // namespace audio

#endif  // AUDIO_UTILS_AUDIO_INPUT_H_
