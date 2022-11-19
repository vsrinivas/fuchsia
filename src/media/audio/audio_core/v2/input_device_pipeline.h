// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V2_INPUT_DEVICE_PIPELINE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V2_INPUT_DEVICE_PIPELINE_H_

#include <fidl/fuchsia.audio.mixer/cpp/wire.h>
#include <fidl/fuchsia.audio/cpp/wire.h>
#include <fidl/fuchsia.media2/cpp/wire.h>
#include <lib/fidl/cpp/wire/client.h>
#include <lib/fit/function.h>

#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "src/media/audio/audio_core/shared/device_config.h"
#include "src/media/audio/audio_core/shared/stream_usage.h"
#include "src/media/audio/audio_core/v2/graph_types.h"
#include "src/media/audio/audio_core/v2/reference_clock.h"
#include "src/media/audio/lib/format2/format.h"

namespace media_audio {

// Represents a pipeline of mixer graph nodes that is sourced from a single input device.
class InputDevicePipeline : public std::enable_shared_from_this<InputDevicePipeline> {
 public:
  struct ProducerArgs {
    // Arguments for Graph.CreateProducer. See comments there for descriptions of these fields.
    std::string name;
    fuchsia_audio::wire::RingBuffer ring_buffer;
    fuchsia_audio_mixer::wire::ExternalDelayWatcher external_delay_watcher;
  };

  struct DeviceArgs {
    // Connection to the mixer service.
    std::shared_ptr<fidl::WireSharedClient<fuchsia_audio_mixer::Graph>> graph_client;

    // Args used to create the ProducerNode which represents this device.
    ProducerArgs producer;

    // Device config.
    media::audio::DeviceConfig::InputDeviceProfile config;

    // Thread which runs this pipeline.
    ThreadId thread;

    // Callback invoked after the output pipeline is constructed.
    fit::callback<void(std::shared_ptr<InputDevicePipeline>)> callback;
  };

  struct LoopbackArgs {
    // Connection to the mixer service.
    std::shared_ptr<fidl::WireSharedClient<fuchsia_audio_mixer::Graph>> graph_client;

    // The SplitterNode which represents this loopback interface.
    NodeId splitter_node;

    // The format of data produced by `splitter_node`.
    Format format;

    // Reference clock used by `splitter_node`.
    ReferenceClock reference_clock;

    // Thread which runs this pipeline.
    ThreadId thread;
  };

  // Creates a new pipeline for the given device. This creates a ProducerNode for `args.producer`.
  // Construction happens asynchronously. Once complete, `args.callback` is invoked. If construction
  // falls, the callback is invoked with `nullptr`.
  static void CreateForDevice(DeviceArgs args);

  // Creates a new pipeline for a loopback device.
  static std::shared_ptr<InputDevicePipeline> CreateForLoopback(LoopbackArgs args);

  // Starts the underlying ProducerNode.
  // REQUIRED: created with `CreateForDevice`
  void Start(fidl::AnyArena& arena, fuchsia_media2::wire::RealTime when,
             fuchsia_media2::wire::StreamTime stream_time);

  // Stops the underlying ProducerNode.
  // REQUIRED: created with `CreateForDevice`
  void Stop(fidl::AnyArena& arena, fuchsia_media2::wire::RealOrStreamTime when);

  // Destroy this pipeline. All nodes will be asynchronously removed from the mixer graph.
  void Destroy();

  // Reports if this pipeline supports capturers with the given `usage`.
  bool SupportsUsage(media::audio::CaptureUsage usage) const;

  // Creates a source node that can accept a destination capturer with the given format. On success,
  // the created node is passed to `callback`. On failure, `callback` receives `std::nullopt`.
  void CreateSourceNodeForFormat(const Format& format,
                                 fit::callback<void(std::optional<NodeId>)> callback);

  // Returns this pipeline's volume curve.
  const media::audio::VolumeCurve& volume_curve() const { return volume_curve_; }

 private:
  InputDevicePipeline(std::shared_ptr<fidl::WireSharedClient<fuchsia_audio_mixer::Graph>> client,
                      media::audio::VolumeCurve volume_curve,
                      media::audio::StreamUsageSet supported_usages, ThreadId thread,
                      ReferenceClock reference_clock)
      : client_(std::move(client)),
        volume_curve_(std::move(volume_curve)),
        supported_usages_(std::move(supported_usages)),
        thread_(thread),
        reference_clock_(std::move(reference_clock)) {}

  std::shared_ptr<fidl::WireSharedClient<fuchsia_audio_mixer::Graph>> client_;
  media::audio::VolumeCurve volume_curve_;
  media::audio::StreamUsageSet supported_usages_;

  ThreadId thread_;
  ReferenceClock reference_clock_;

  // If an input pipeline is rooted at an input device, it looks like:
  //
  // ```
  // ProducerNode
  //    -> SplitterNode
  //          +--> {ConsumerNode, ...}
  //          +--> MixerNode -> SplitterNode -> {ConsumerNode, ...}
  //          +--> MixerNode -> SplitterNode -> {ConsumerNode, ...}
  //          ...
  // ```
  //
  // If an input pipeline is rooted at a loopback device, it looks like:
  //
  // ```
  // SplitterNode
  //    +--> {ConsumerNode, ...}
  //    +--> MixerNode -> SplitterNode -> {ConsumerNode, ...}
  //    +--> MixerNode -> SplitterNode -> {ConsumerNode, ...}
  //    ...
  // ```
  //
  // In both diagrams, a ConsumerNode connects to the SplitterNode which has a compatible format,
  // where two formats are "compatible" if they have the same frame rate and channelization, i.e. if
  // they are equivalent ignoring `sample_type`. This structure avoids unnecessary recomputation.
  //
  // This is the root SplitterNode in the above diagrams.
  NodeId root_splitter_;

  // This is the ProducerNode if rooted at an input device, or nullopt for loopback devices.
  std::optional<NodeId> producer_node_;

  // This maps `(channel_count, frames_per_second)` to the SplitterNode which produces that format.
  // Using `std::map` instead of `std::unordered_map` because `std::pair` does not have a default
  // hash function, plus this should not have very many keys in practice.
  std::map<std::pair<int64_t, int64_t>, NodeId> splitters_by_format_;

  // All nodes created by this pipeline.
  std::unordered_set<NodeId> created_nodes_;

  bool started_ = false;
  bool pending_start_ = false;
  bool pending_stop_ = false;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V2_INPUT_DEVICE_PIPELINE_H_
