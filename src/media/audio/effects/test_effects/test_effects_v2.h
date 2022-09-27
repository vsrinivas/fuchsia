// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_EFFECTS_TEST_EFFECTS_TEST_EFFECTS_V2_H_
#define SRC_MEDIA_AUDIO_EFFECTS_TEST_EFFECTS_TEST_EFFECTS_V2_H_

#include <fidl/fuchsia.audio.effects/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/component/cpp/service_client.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace media::audio {

// This class provides a simple interface for constructing ProcessorCreator servers in tests.
class TestEffectsV2 : public fidl::WireServer<fuchsia_audio_effects::ProcessorCreator> {
 public:
  // If the dispatcher is not specified, use an internal dispatcher that runs on a separate thread.
  explicit TestEffectsV2(async_dispatcher_t* dispatcher = nullptr);
  ~TestEffectsV2() override;

  using ProcessFn = std::function<zx_status_t(
      uint64_t num_frames, float* input, float* output, float total_applied_gain_for_input,
      std::vector<fuchsia_audio_effects::wire::ProcessMetrics>& metrics)>;

  struct Effect {
    std::string name;

    // Implementation of this effect.
    ProcessFn process;

    // Parameters.
    bool process_in_place = false;
    uint64_t max_frames_per_call = 0;  // if zero, use default
    uint64_t block_size_frames = 0;    // if zero, use default
    uint64_t latency_frames = 0;
    uint64_t ring_out_frames = 0;
    uint32_t frames_per_second = 0;  // must specify
    uint32_t input_channels = 0;     // must specify
    uint32_t output_channels = 0;    // must specify
  };

  // Creates a new effect. The name must be unique.
  zx_status_t AddEffect(Effect effect);

  // Removes all effects. This will close all open processor channels with ZX_ERR_PEER_CLOSED.
  // Must not call concurrently with effects processing.
  zx_status_t ClearEffects();

  // Create a client connection to the ProcessorCreator server held by this class.
  fidl::ClientEnd<fuchsia_audio_effects::ProcessorCreator> NewClient();

  // Handle an incoming client request.
  void HandleRequest(fidl::ServerEnd<fuchsia_audio_effects::ProcessorCreator> server_end);

 private:
  class TestProcessor;

  // Implements the FIDL API.
  void Create(CreateRequestView request, CreateCompleter::Sync& completer) override;

  async_dispatcher_t* dispatcher_;
  std::unique_ptr<async::Loop> loop_;
  std::vector<fidl::ServerBindingRef<fuchsia_audio_effects::ProcessorCreator>> bindings_;
  std::unordered_map<std::string, Effect> effects_;
  std::unordered_set<std::unique_ptr<TestProcessor>> processors_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_EFFECTS_TEST_EFFECTS_TEST_EFFECTS_V2_H_
