// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <audio-utils/audio-stream.h>
#include <magenta/types.h>

#include "wav-common.h"

class WAVSource : public WAVCommon,
                  public audio::utils::AudioSource {
public:
    WAVSource() { }
    mx_status_t Initialize(const char* filename);

    // AudioSource interface
    mx_status_t GetFormat(AudioStream::Format* out_format) final;
    mx_status_t GetFrames(void* buffer, uint32_t buf_space, uint32_t* out_packed) final;
    bool finished() const final { return payload_played_ >= payload_len_; }

private:
    uint32_t payload_len_ = 0;
    uint32_t payload_played_ = 0;
    AudioStream::Format audio_format_;
};

