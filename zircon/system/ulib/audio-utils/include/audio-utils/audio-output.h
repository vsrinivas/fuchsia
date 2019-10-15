// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef AUDIO_UTILS_AUDIO_OUTPUT_H_
#define AUDIO_UTILS_AUDIO_OUTPUT_H_

#include <zircon/types.h>

#include <memory>

#include <audio-utils/audio-device-stream.h>

namespace audio {
namespace utils {

class AudioSource;

class AudioOutput : public AudioDeviceStream {
 public:
  static std::unique_ptr<AudioOutput> Create(uint32_t dev_id);
  static std::unique_ptr<AudioOutput> Create(const char* dev_path);
  zx_status_t Play(AudioSource& source);

 private:
  friend class std::default_delete<AudioOutput>;
  friend class AudioDeviceStream;

  explicit AudioOutput(uint32_t dev_id) : AudioDeviceStream(StreamDirection::kOutput, dev_id) {}
  explicit AudioOutput(const char* dev_path)
      : AudioDeviceStream(StreamDirection::kOutput, dev_path) {}
};

}  // namespace utils
}  // namespace audio

#endif  // AUDIO_UTILS_AUDIO_OUTPUT_H_
