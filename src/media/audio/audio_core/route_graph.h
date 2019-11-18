// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_ROUTE_GRAPH_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_ROUTE_GRAPH_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fit/bridge.h>

#include <deque>
#include <unordered_map>

#include <fbl/ref_ptr.h>

#include "src/media/audio/audio_core/audio_input.h"
#include "src/media/audio/audio_core/audio_output.h"
#include "src/media/audio/audio_core/routing_config.h"
#include "src/media/audio/audio_core/threading_model.h"

namespace media::audio {

// |RoutingProfile| informs the |RouteGraph| on how or whether it should establish links for a
// particular input or output to the mixer.
struct RoutingProfile {
  bool routable = false;
};

// |RouteGraph| is responsible for managing connections between inputs and outputs of the mixer.
//
// |RouteGraph| owns user-level inputs and outputs (|AudioRenderer|s and |AudioCapturer|s).
class RouteGraph {
 public:
  RouteGraph(const RoutingConfig& routing_config);

  ~RouteGraph();

  // TODO(13339): Remove throttle_output_.
  // Sets a throttle output which is linked to all AudioRenderers to throttle the rate at which we
  // return packets to clients.
  void SetThrottleOutput(ThreadingModel* threading_model, fbl::RefPtr<AudioOutput> throttle_output);

  // Adds an |AudioOutput| to the route graph. An |AudioOutput| is allowed to receive
  // postmix samples from |AudioRenderer|s.
  void AddOutput(AudioDevice* output);

  // Removes an |AudioOutput| from the route graph. Any connected |AudioRenderer|s and loopback
  // |AudioCapturer|s will be rerouted.
  void RemoveOutput(AudioDevice* output);

  // Adds an |AudioInput| to the route graph. An audio input may be connected to transmit samples
  // to an |AudioCapturer|.
  void AddInput(AudioDevice* input);

  // Removes an |AudioInput| from the route graph. Any connected |AudioCapturer|s will be rerouted.
  void RemoveInput(AudioDevice* input);

  // Adds an |AudioRenderer| to the route graph. An |AudioRenderer| may be connected to
  // |AudioOutput|s.
  void AddRenderer(fbl::RefPtr<AudioObject> renderer);

  // Sets the routing profile with which the route graph selects |AudioOutput|s for the
  // |AudioRenderer|.
  void SetRendererRoutingProfile(AudioObject* renderer, RoutingProfile profile);

  void RemoveRenderer(AudioObject* renderer);

  // Adds an |AudioCapturer| to the route graph. An |AudioCapturer| may be connected to
  // |AudioInput|s to receive samples from them.
  void AddCapturer(fbl::RefPtr<AudioObject> capturer);

  // Sets the routing profile with which the route graph selects |AudioInput|s for the
  // |AudioCapturer|.
  void SetCapturerRoutingProfile(AudioObject* capturer, RoutingProfile profile);

  void RemoveCapturer(AudioObject* capturer);

  // Adds an |AudioCapturer| to the route graph which will receive the output mixed for the most
  // recently added output device.
  void AddLoopbackCapturer(fbl::RefPtr<AudioObject> capturer);

  // Sets the routing profile with which the route graph selects |AudioOutput|s for the
  // loopback |AudioCapturer|.
  void SetLoopbackCapturerRoutingProfile(AudioObject* capturer, RoutingProfile profile);

  void RemoveLoopbackCapturer(AudioObject* capturer);

 private:
  struct RoutableOwnedObject {
    fbl::RefPtr<AudioObject> ref;
    RoutingProfile profile;
  };

  void LinkRenderersTo(AudioDevice* output);
  void LinkCapturersTo(AudioDevice* input);
  void LinkLoopbackCapturersTo(AudioDevice* output);

  [[maybe_unused]] const RoutingConfig& routing_config_;

  // TODO(39624): convert to weak_ptr when ownership is explicit.
  std::deque<AudioDevice*> inputs_;
  std::deque<AudioDevice*> outputs_;

  std::unordered_map<AudioObject*, RoutableOwnedObject> capturers_;
  std::unordered_map<AudioObject*, RoutableOwnedObject> renderers_;
  std::unordered_map<AudioObject*, RoutableOwnedObject> loopback_capturers_;

  // TODO(13339): Remove throttle_output_.
  std::optional<fit::completer<void, void>> throttle_release_fence_;
  fbl::RefPtr<AudioOutput> throttle_output_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_ROUTE_GRAPH_H_
