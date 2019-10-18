// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_ROUTING_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_ROUTING_H_

#include <fuchsia/media/cpp/fidl.h>

#include "src/media/audio/audio_core/audio_output.h"
#include "src/media/audio/audio_core/audio_renderer_impl.h"

namespace media::audio {

class Routing {
 public:
  virtual ~Routing() = default;

  // Select the initial set of outputs for a newly-configured AudioRenderer.
  virtual void SelectOutputsForAudioRenderer(AudioRendererImpl* renderer) = 0;

  // // Link an output to an AudioRenderer.
  virtual void LinkOutputToAudioRenderer(AudioOutput* output, AudioRendererImpl* renderer) = 0;

  virtual void SetRoutingPolicy(fuchsia::media::AudioOutputRoutingPolicy policy) = 0;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_ROUTING_H_
