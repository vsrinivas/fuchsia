// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_ROUTING_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_ROUTING_H_

#include <set>

#include "src/lib/fxl/logging.h"
#include "src/media/audio/audio_core/audio_link.h"
#include "src/media/audio/audio_core/routing.h"
#include "src/media/audio/audio_core/testing/fake_audio_device.h"

namespace media::audio::testing {

class FakeRouting : public Routing {
 public:
  ~FakeRouting() override = default;

  void AddOutputForRenderer(fbl::RefPtr<AudioDevice> fake_output) {
    outputs_.insert(fake_output.get());
  }

  // Routing implementation
  void SelectOutputsForAudioRenderer(AudioRendererImpl* renderer) override {
    FXL_CHECK(renderer);
    FXL_CHECK(renderer->format_info_valid());

    for (auto& output : outputs_) {
      LinkOutputToAudioRenderer(reinterpret_cast<AudioOutput*>(output), renderer);
    }

    renderer->RecomputeMinClockLeadTime();
  }

  void LinkOutputToAudioRenderer(AudioOutput* output, AudioRendererImpl* renderer) override {
    AudioObject::LinkObjects(fbl::RefPtr(renderer), fbl::RefPtr(output));
  }

  void SetRoutingPolicy(fuchsia::media::AudioOutputRoutingPolicy) override {}

 private:
  std::unordered_set<AudioDevice*> outputs_;
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_ROUTING_H_
