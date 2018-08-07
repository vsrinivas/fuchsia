// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_CORE_MIXER_POINT_SAMPLER_H_
#define GARNET_BIN_MEDIA_AUDIO_CORE_MIXER_POINT_SAMPLER_H_

#include <fuchsia/media/cpp/fidl.h>

#include "garnet/bin/media/audio_core/mixer/mixer.h"

namespace media {
namespace audio {
namespace mixer {

class PointSampler : public Mixer {
 public:
  static MixerPtr Select(const fuchsia::media::AudioStreamType& src_format,
                         const fuchsia::media::AudioStreamType& dst_format);

 protected:
  PointSampler(uint32_t pos_filter_width, uint32_t neg_filter_width)
      : Mixer(pos_filter_width, neg_filter_width) {}
};

}  // namespace mixer
}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_CORE_MIXER_POINT_SAMPLER_H_
