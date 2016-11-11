// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/services/media_types.fidl.h"
#include "apps/media/src/audio_server/platform/generic/mixer.h"

namespace media {
namespace audio {
namespace mixers {

class LinearSampler : public Mixer {
 public:
  static MixerPtr Select(const AudioMediaTypeDetailsPtr& src_format,
                         const AudioMediaTypeDetailsPtr& dst_format);

 protected:
  LinearSampler(uint32_t pos_filter_width, uint32_t neg_filter_width)
      : Mixer(pos_filter_width, neg_filter_width) {}
};

}  // namespace mixers
}  // namespace audio
}  // namespace media
