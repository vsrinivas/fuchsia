// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V2_OUTPUT_DEVICE_PIPELINE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V2_OUTPUT_DEVICE_PIPELINE_H_

#include <fidl/fuchsia.audio.mixer/cpp/wire.h>
#include <fidl/fuchsia.audio/cpp/wire.h>
#include <fidl/fuchsia.media2/cpp/wire.h>
#include <lib/fidl/cpp/wire/client.h>
#include <lib/fit/function.h>

#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "src/media/audio/audio_core/shared/device_config.h"
#include "src/media/audio/audio_core/shared/stream_usage.h"
#include "src/media/audio/audio_core/v2/graph_types.h"
#include "src/media/audio/audio_core/v2/input_device_pipeline.h"
#include "src/media/audio/lib/effects_loader/effects_loader_v2.h"

namespace media_audio {

// Represents a pipeline of mixer graph nodes that feed into a single output device.
class OutputDevicePipeline : public std::enable_shared_from_this<OutputDevicePipeline> {
 public:
  struct ConsumerArgs {
    // Arguments for Graph.CreateConsumer. See comments there for descriptions of these fields.
    std::string name;
    ThreadId thread;
    fuchsia_audio::wire::RingBuffer ring_buffer;
    fuchsia_audio_mixer::wire::ExternalDelayWatcher external_delay_watcher;
  };

  struct Args {
    // Connection to the mixer service.
    std::shared_ptr<fidl::WireSharedClient<fuchsia_audio_mixer::Graph>> graph_client;

    // Args used to create the ConsumerNode which represents this device.
    ConsumerArgs consumer;

    // Device config.
    media::audio::DeviceConfig::OutputDeviceProfile config;

    // For loading effects configs.
    std::unique_ptr<media::audio::EffectsLoaderV2> effects_loader;

    // Callback invoked after the output pipeline is constructed.
    fit::callback<void(std::shared_ptr<OutputDevicePipeline>)> callback;
  };

  // Creates a new pipeline for the given device. This creates a ConsumerNode for `args.consumer`,
  // then constructs a pipeline from `args.config`. Construction happens asynchronously. Once
  // complete, `args.callback` is invoked. If construction falls, the callback is invoked with
  // `nullptr`.
  static void Create(Args args);

  // Starts the underlying ConsumerNode.
  void Start(fidl::AnyArena& arena, fuchsia_media2::wire::RealTime when,
             fuchsia_media2::wire::StreamTime stream_time);

  // Stops the underlying ConsumerNode.
  void Stop(fidl::AnyArena& arena, fuchsia_media2::wire::RealOrStreamTime when);

  // Destroy this pipeline. All nodes will be asynchronously removed from the mixer graph.
  void Destroy();

  // Reports if this pipeline supports renderers with the given `usage`.
  bool SupportsUsage(media::audio::RenderUsage usage) const;

  // Returns a destination node that can accept a source renderer with the given usage. The returned
  // node can accept an arbitrarily large number of renderers.
  //
  // REQUIRED: `SupportsUsage(usage)`.
  NodeId DestNodeForUsage(media::audio::RenderUsage usage) const;

  // Returns the loopback interface, or `nullptr` if this output pipeline does not support loopback.
  std::shared_ptr<InputDevicePipeline> loopback() const { return loopback_; }

  // Returns this pipeline's volume curve.
  const media::audio::VolumeCurve& volume_curve() const { return volume_curve_; }

 private:
  OutputDevicePipeline(std::shared_ptr<fidl::WireSharedClient<fuchsia_audio_mixer::Graph>> client,
                       std::shared_ptr<InputDevicePipeline> loopback,
                       media::audio::VolumeCurve volume_curve, NodeId consumer_node,
                       std::unordered_map<media::audio::RenderUsage, NodeId> usage_to_dest_node,
                       std::unordered_set<NodeId> created_nodes)
      : client_(std::move(client)),
        loopback_(std::move(loopback)),
        volume_curve_(std::move(volume_curve)),
        consumer_node_(consumer_node),
        usage_to_dest_node_(std::move(usage_to_dest_node)),
        created_nodes_(std::move(created_nodes)) {}

  std::shared_ptr<fidl::WireSharedClient<fuchsia_audio_mixer::Graph>> client_;
  std::shared_ptr<InputDevicePipeline> loopback_;
  media::audio::VolumeCurve volume_curve_;

  // An output pipeline is an inverted tree of arbitrary depth, where the root of the tree is a
  // ConsumerNode (representing the device) and the leaves are MixerNodes which can be connected by
  // renderers. In between are CustomNodes and at most one SplitterNode, which represents the
  // loopback interface. Each RenderUsage maps to a unique MixerNode; all renderers with the same
  // usage connect to the same MixerNode. This looks like:
  //
  // ```
  // Renderer --+-> MixerNode --+
  // Renderer --+               |
  // ...                        +--> ... --> ConsumerNode
  //                            |
  // Renderer --+-> MixerNode --+
  // Renderer --+
  // ...
  // ```
  //
  // This is the ConsumerNode in the above diagram.
  NodeId consumer_node_;

  // Maps each usage to a MixerNode.
  std::unordered_map<media::audio::RenderUsage, NodeId> usage_to_dest_node_;

  // All nodes created by this pipeline.
  std::unordered_set<NodeId> created_nodes_;

  bool started_ = false;
  bool pending_start_ = false;
  bool pending_stop_ = false;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V2_OUTPUT_DEVICE_PIPELINE_H_
