// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_POINT_SAMPLER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_POINT_SAMPLER_H_

#include <fuchsia/media/cpp/fidl.h>

#include <memory>
#include <utility>

#include "src/media/audio/audio_core/mixer/mixer.h"
#include "src/media/audio/lib/processing/sampler.h"

namespace media::audio::mixer {

class PointSampler : public Mixer {
 public:
  static std::unique_ptr<Mixer> Select(const fuchsia::media::AudioStreamType& source_format,
                                       const fuchsia::media::AudioStreamType& dest_format,
                                       Gain::Limits gain_limits = {});

  void Mix(float* dest_ptr, int64_t dest_frames, int64_t* dest_offset_ptr,
           const void* source_void_ptr, int64_t source_frames, Fixed* source_offset_ptr,
           bool accumulate) override;

 protected:
  PointSampler(Fixed pos_filter_width, Fixed neg_filter_width, Gain::Limits gain_limits,
               std::shared_ptr<media_audio::Sampler> point_sampler)
      : Mixer(pos_filter_width, neg_filter_width, gain_limits),
        point_sampler_(std::move(point_sampler)) {}

 private:
  std::shared_ptr<media_audio::Sampler> point_sampler_;
};

}  // namespace media::audio::mixer

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_POINT_SAMPLER_H_
