// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/audio_renderer_impl.h"

#include "garnet/bin/media/audio_server/audio_output.h"

namespace media {
namespace audio {

AudioRendererImpl::AudioRendererImpl() : AudioObject(Type::Renderer) {}

void AudioRendererImpl::SetThrottleOutput(
    std::shared_ptr<AudioLinkPacketSource> throttle_output_link) {
  FXL_DCHECK(throttle_output_link != nullptr);
  FXL_DCHECK(throttle_output_link_ == nullptr);
  throttle_output_link_ = std::move(throttle_output_link);
}

void AudioRendererImpl::RecomputeMinClockLeadTime() {
  int64_t cur_lead_time = 0;

  {
    fbl::AutoLock lock(&links_lock_);
    for (const auto& link : dest_links_) {
      if (link->GetDest()->is_output()) {
        const auto& output = *static_cast<AudioOutput*>(link->GetDest().get());
        if (cur_lead_time < output.min_clock_lead_time_nsec()) {
          cur_lead_time = output.min_clock_lead_time_nsec();
        }
      }
    }
  }

  if (min_clock_lead_nsec_ != cur_lead_time) {
    min_clock_lead_nsec_ = cur_lead_time;
    ReportNewMinClockLeadTime();
  }
}

}  // namespace audio
}  // namespace media
