// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/src/audio_server/platform/generic/mixer.h"

namespace mojo {
namespace media {
namespace audio {
namespace mixers {

class NoOp : public Mixer {
 public:
  NoOp() : Mixer(0, 0) {}

  bool Mix(int32_t* dst,
           uint32_t dst_frames,
           uint32_t* dst_offset,
           const void* src,
           uint32_t frac_src_frames,
           int32_t* frac_src_offset,
           uint32_t frac_step_size,
           Gain::AScale amplitude_scale,
           bool accumulate) override;
};

}  // namespace mixers
}  // namespace audio
}  // namespace media
}  // namespace mojo
