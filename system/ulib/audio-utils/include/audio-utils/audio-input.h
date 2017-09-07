// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <audio-utils/audio-device-stream.h>
#include <magenta/types.h>

namespace audio {
namespace utils {

class AudioSink;

class AudioInput : public AudioDeviceStream {
public:
    static fbl::unique_ptr<AudioInput> Create(uint32_t dev_id);
    static fbl::unique_ptr<AudioInput> Create(const char* dev_path);
    mx_status_t Record(AudioSink& sink, float duration_seconds);

private:
    friend class fbl::unique_ptr<AudioInput>;
    friend class AudioDeviceStream;

    explicit AudioInput(uint32_t dev_id) : AudioDeviceStream(true, dev_id) { }
    explicit AudioInput(const char* dev_path) : AudioDeviceStream(true, dev_path) { }
};

}  // namespace utils
}  // namespace audio
