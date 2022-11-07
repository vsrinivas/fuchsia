// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/shared/mixer/no_op.h"

namespace media::audio::mixer {

void NoOp::Mix(float* dest, int64_t dest_frames, int64_t* dest_offset, const void* source_void_ptr,
               int64_t source_frames, Fixed* source_offset_ptr, bool accumulate) {}

}  // namespace media::audio::mixer
