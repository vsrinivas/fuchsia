// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <magenta/device/audio.h>

namespace audio {
namespace utils {

class AudioStream {
public:
    struct Format {
        uint32_t frame_rate;
        uint16_t channels;
        audio_sample_format_t sample_format;
    };
};

class AudioSource : public AudioStream {
public:
    virtual mx_status_t GetFormat(Format* out_format) = 0;
    virtual mx_status_t GetFrames(void* buffer, uint32_t buf_space, uint32_t* out_packed) = 0;
    virtual bool finished() const = 0;
};

class AudioSink : public AudioStream {
public:
    virtual mx_status_t SetFormat(const Format& format) = 0;
    virtual mx_status_t PutFrames(const void* buffer, uint32_t amt) = 0;
    virtual mx_status_t Finalize() = 0;
};

}  // namespace utils
}  // namespace audio
