// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <audio-utils/audio-stream.h>
#include <magenta/types.h>

class SineSource : public audio::utils::AudioSource {
public:
    SineSource() { }

    mx_status_t Init(float freq,
                     float amp,
                     float duration_secs,
                     uint32_t frame_rate,
                     uint32_t channels,
                     audio_sample_format_t sample_format);

    mx_status_t GetFormat(Format* out_format) final;
    mx_status_t GetFrames(void* buffer, uint32_t buf_space, uint32_t* out_packed);
    bool finished() const final { return (frames_produced_ >= frames_to_produce_); }

private:
    using GetFramesThunk = mx_status_t (SineSource::*)(void* buffer,
                                                       uint32_t buf_space,
                                                       uint32_t* out_packed);

    template <audio_sample_format_t SAMPLE_FORMAT>
    mx_status_t InitInternal();

    template <audio_sample_format_t SAMPLE_FORMAT>
    mx_status_t GetFramesInternal(void* buffer, uint32_t buf_space, uint32_t* out_packed);

    uint64_t frames_to_produce_;
    uint64_t frames_produced_;
    double   amp_;
    double   sine_scalar_;
    uint32_t frame_rate_;
    uint32_t channels_;
    uint32_t frame_size_;
    audio_sample_format_t sample_format_;
    GetFramesThunk get_frames_thunk_ = nullptr;
};
