// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

#include "audio-stream.h"

class AudioInput : public AudioStream {
public:
    // TODO(johngro) : Add record-to-wav-file functionality

private:
    friend class mxtl::unique_ptr<AudioInput>;
    friend class AudioStream;

    explicit AudioInput(uint32_t dev_id) : AudioStream(true, dev_id) { }
};
