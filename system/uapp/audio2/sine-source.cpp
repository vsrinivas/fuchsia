// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/assert.h>
#include <math.h>
#include <mxtl/algorithm.h>
#include <mxtl/limits.h>

#include "sine-source.h"

constexpr uint32_t FRAME_RATE = 48000;
constexpr uint32_t CHANNELS = 2;
constexpr audio2_sample_format_t SAMPLE_FORMAT = AUDIO2_SAMPLE_FORMAT_16BIT;
constexpr uint32_t FRAME_SIZE = 4;

SineSource::SineSource(float freq, float amp, float duration_secs) {
    frames_to_produce_ = (duration_secs == 0.0)
                       ? mxtl::numeric_limits<uint64_t>::max()
                       : static_cast<uint64_t>(duration_secs * FRAME_RATE);
    sine_scalar_ = (freq * 2.0 * M_PI) / FRAME_RATE;
    amp_ = mxtl::clamp<double>(amp, 0.0, 1.0) * 0x7ffe;
}

mx_status_t SineSource::GetFormat(Format* out_format) {
    if (out_format == nullptr)
        return MX_ERR_INVALID_ARGS;

    out_format->frame_rate    = FRAME_RATE;
    out_format->channels      = CHANNELS;
    out_format->sample_format = SAMPLE_FORMAT;

    return MX_OK;
}

mx_status_t SineSource::PackFrames(void* buffer, uint32_t buf_space, uint32_t* out_packed) {
    if ((buffer == nullptr) || (out_packed == nullptr))
        return MX_ERR_INVALID_ARGS;

    if (finished())
        return MX_ERR_BAD_STATE;

    MX_DEBUG_ASSERT(frames_produced_ < frames_to_produce_);
    uint64_t todo = mxtl::min<uint64_t>(frames_to_produce_ - frames_produced_,
                                        buf_space / FRAME_SIZE);
    double pos = sine_scalar_ * static_cast<double>(frames_produced_);
    auto   buf = reinterpret_cast<uint32_t*>(buffer);

    for (uint64_t i = 0; i < todo; ++i) {
        int16_t  val  = static_cast<int16_t>(amp_ * sin(pos));
        uint32_t bits = static_cast<uint32_t>(static_cast<uint16_t>(val));

        bits |= bits << 16;
        buf[i] = bits;
        pos += sine_scalar_;
    }

    *out_packed = static_cast<uint32_t>(todo * FRAME_SIZE);
    frames_produced_ += todo;

    return MX_OK;
}
