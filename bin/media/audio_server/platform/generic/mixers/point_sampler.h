// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/media/audio_server/platform/generic/mixer.h"
#include "lib/media/fidl/media_types.fidl.h"

namespace media {
namespace audio {
namespace mixers {

class PointSampler : public Mixer {
 public:
  static MixerPtr Select(const AudioMediaTypeDetailsPtr& src_format,
                         const AudioMediaTypeDetailsPtr& dst_format);

 protected:
  PointSampler(uint32_t pos_filter_width, uint32_t neg_filter_width)
      : Mixer(pos_filter_width, neg_filter_width) {}
};

}  // namespace mixers
}  // namespace audio
}  // namespace media
