// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/mixer/no_op.h"

namespace media {
namespace audio {
namespace mixer {

bool NoOp::Mix(float* dest, uint32_t dest_frames, uint32_t* dest_offset,
               const void* src, uint32_t frac_src_frames,
               int32_t* frac_src_offset, uint32_t frac_step_size,
               Gain::AScale amplitude_scale, bool accumulate,
               uint32_t rate_modulo, uint32_t denominator,
               uint32_t* src_pos_modulo) {
  return false;
}

}  // namespace mixer
}  // namespace audio
}  // namespace media
