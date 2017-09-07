// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/assert.h>
#include <math.h>
#include <fbl/algorithm.h>
#include <fbl/limits.h>

#include "sine-source.h"

mx_status_t SineSource::Init(float freq,
                             float amp,
                             float duration_secs,
                             uint32_t frame_rate,
                             uint32_t channels,
                             audio_sample_format_t sample_format) {

    if (!frame_rate)
        return MX_ERR_INVALID_ARGS;

    if (!channels)
        return MX_ERR_INVALID_ARGS;

    frame_rate_ = frame_rate;
    channels_ = channels;

    frames_to_produce_ = (duration_secs == 0.0)
                       ? fbl::numeric_limits<uint64_t>::max()
                       : static_cast<uint64_t>(duration_secs * static_cast<float>(frame_rate_));
    sine_scalar_ = (freq * 2.0 * M_PI) / frame_rate_;
    amp_ = fbl::clamp<double>(amp, 0.0, 1.0);

    switch(static_cast<audio_sample_format_t>(sample_format & ~AUDIO_SAMPLE_FORMAT_FLAG_MASK)) {
    case AUDIO_SAMPLE_FORMAT_8BIT:       return InitInternal<AUDIO_SAMPLE_FORMAT_8BIT>();
    case AUDIO_SAMPLE_FORMAT_16BIT:      return InitInternal<AUDIO_SAMPLE_FORMAT_16BIT>();
    case AUDIO_SAMPLE_FORMAT_20BIT_IN32: return InitInternal<AUDIO_SAMPLE_FORMAT_20BIT_IN32>();
    case AUDIO_SAMPLE_FORMAT_24BIT_IN32: return InitInternal<AUDIO_SAMPLE_FORMAT_24BIT_IN32>();
    case AUDIO_SAMPLE_FORMAT_32BIT:      return InitInternal<AUDIO_SAMPLE_FORMAT_32BIT>();
    default:                             return MX_ERR_INVALID_ARGS;
    }
}

mx_status_t SineSource::GetFormat(Format* out_format) {
    if (out_format == nullptr)
        return MX_ERR_INVALID_ARGS;

    out_format->frame_rate    = frame_rate_;
    out_format->channels      = static_cast<uint16_t>(channels_);
    out_format->sample_format = sample_format_;

    return MX_OK;
}

mx_status_t SineSource::GetFrames(void* buffer, uint32_t buf_space, uint32_t* out_packed) {
    MX_DEBUG_ASSERT(get_frames_thunk_ != nullptr);
    return ((*this).*(get_frames_thunk_))(buffer, buf_space, out_packed);
}

namespace {

template <audio_sample_format_t SAMPLE_FORMAT>
struct SampleTraits;

template <>
struct SampleTraits<AUDIO_SAMPLE_FORMAT_8BIT> {
    using SampleType   = uint8_t;
    using ComputedType = int8_t;
    static SampleType encode(ComputedType v) {
        return static_cast<ComputedType>(static_cast<SampleType>(v) + 0x80);
    }
};

template <>
struct SampleTraits<AUDIO_SAMPLE_FORMAT_16BIT> {
    using SampleType   = int16_t;
    using ComputedType = int16_t;
    static SampleType encode(ComputedType v) { return v; }
};

template <>
struct SampleTraits<AUDIO_SAMPLE_FORMAT_20BIT_IN32> {
    using SampleType   = int32_t;
    using ComputedType = int32_t;
    static SampleType encode(ComputedType v) {
        return static_cast<SampleType>(static_cast<uint32_t>(v) & 0xFFFFF000);
    }
};

template <>
struct SampleTraits<AUDIO_SAMPLE_FORMAT_24BIT_IN32> {
    using SampleType   = int32_t;
    using ComputedType = int32_t;
    static SampleType encode(ComputedType v) {
        return static_cast<SampleType>(static_cast<uint32_t>(v) & 0xFFFFFF00);
    }
};

template <>
struct SampleTraits<AUDIO_SAMPLE_FORMAT_32BIT> {
    using SampleType   = int32_t;
    using ComputedType = int32_t;
    static SampleType encode(ComputedType v) { return v; }
};

} // Anon namespace

template <audio_sample_format_t SAMPLE_FORMAT>
mx_status_t SineSource::InitInternal() {
    using SampleType   = typename SampleTraits<SAMPLE_FORMAT>::SampleType;
    using ComputedType = typename SampleTraits<SAMPLE_FORMAT>::ComputedType;

    sample_format_ = SAMPLE_FORMAT;
    get_frames_thunk_ = &SineSource::GetFramesInternal<SAMPLE_FORMAT>;
    frame_size_ = static_cast<uint32_t>(sizeof(SampleType) * channels_);
    amp_ *= fbl::numeric_limits<ComputedType>::max() - 1;

    return MX_OK;
}

template <audio_sample_format_t SAMPLE_FORMAT>
mx_status_t SineSource::GetFramesInternal(void* buffer, uint32_t buf_space, uint32_t* out_packed) {
    using Traits       = SampleTraits<SAMPLE_FORMAT>;
    using SampleType   = typename SampleTraits<SAMPLE_FORMAT>::SampleType;
    using ComputedType = typename SampleTraits<SAMPLE_FORMAT>::ComputedType;

    if ((buffer == nullptr) || (out_packed == nullptr))
        return MX_ERR_INVALID_ARGS;

    if (finished())
        return MX_ERR_BAD_STATE;

    MX_DEBUG_ASSERT(frames_produced_ < frames_to_produce_);
    uint64_t todo = fbl::min<uint64_t>(frames_to_produce_ - frames_produced_,
                                        buf_space / frame_size_);
    double pos = sine_scalar_ * static_cast<double>(frames_produced_);
    auto   buf = reinterpret_cast<SampleType*>(buffer);

    for (uint64_t i = 0; i < todo; ++i) {
        auto val = static_cast<ComputedType>(amp_ * sin(pos));

        for (uint32_t j = 0; j < channels_; ++j)
            *(++buf) = Traits::encode(val);

        pos += sine_scalar_;
    }

    *out_packed = static_cast<uint32_t>(todo * frame_size_);
    frames_produced_ += todo;

    return MX_OK;
}
