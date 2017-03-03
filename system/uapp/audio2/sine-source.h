// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

#include "audio-source.h"

class SineSource : public AudioSource {
public:
    SineSource(float freq, float amp, float duration_secs);

    mx_status_t GetFormat(Format* out_format) final;
    mx_status_t PackFrames(void* buffer, uint32_t buf_space, uint32_t* out_packed) final;
    bool finished() const final {
        return (frames_produced_ >= frames_to_produce_);
    }

private:
    uint64_t frames_to_produce_;
    uint64_t frames_produced_ = 0;
    double   sine_scalar_;
    double   amp_;
};
