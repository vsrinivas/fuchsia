// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/audio_renderer_impl.h"

namespace media {
namespace audio {

AudioRendererImpl::AudioRendererImpl()
  : AudioObject(Type::Renderer) { }

void AudioRendererImpl::SetThrottleOutput(
      std::shared_ptr<AudioLinkPacketSource> throttle_output_link) {
  FXL_DCHECK(throttle_output_link != nullptr);
  FXL_DCHECK(throttle_output_link_ == nullptr);
  throttle_output_link_ = std::move(throttle_output_link);
}

}  // namespace audio
}  // namespace media
