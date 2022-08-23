// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_STREAM_SINK_PRODUCER_NODE_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_STREAM_SINK_PRODUCER_NODE_H_

#include "src/media/audio/services/mixer/fidl/node.h"
#include "src/media/audio/services/mixer/fidl_realtime/stream_sink_server.h"

namespace media_audio {

// This is a meta node driven by a StreamSinkServer. Since this is a producer, it has no child
// source nodes. The child destination nodes are all PacketQueueProducerNodes. Packets received by
// the StreamSink are copied to every child over a CommandQueue.
class StreamSinkProducerNode : public Node,
                               public std::enable_shared_from_this<StreamSinkProducerNode> {
 public:
  struct Args {
    // Name of this node.
    std::string_view name;

    // Reference clock of this nodes's destination streams.
    zx_koid_t reference_clock_koid;

    // FIDL server that drivers this producer.
    std::shared_ptr<StreamSinkServer> stream_sink_server;

    // On creation, child nodes are initially assigned to this DetachedThread.
    DetachedThreadPtr detached_thread;
  };

  static std::shared_ptr<StreamSinkProducerNode> Create(Args args);

  // Starts this producer. The command is forwarded to each outgoing CommandQueue.
  void Start(PacketQueueProducerStage::StartCommand cmd) const;

  // Stops this producer. The command is forwarded to each outgoing CommandQueue.
  void Stop(PacketQueueProducerStage::StopCommand cmd) const;

 private:
  using CommandQueue = PacketQueueProducerStage::CommandQueue;

  StreamSinkProducerNode(Args args)
      : Node(args.name, /*is_meta=*/true, /*pipeline_stage=*/nullptr, /*parent=*/nullptr),
        reference_clock_koid_(args.reference_clock_koid),
        stream_sink_server_(std::move(args.stream_sink_server)),
        detached_thread_(std::move(args.detached_thread)) {}

  // Implementation of Node.
  NodePtr CreateNewChildSource() final;
  NodePtr CreateNewChildDest() final;
  void DestroyChildDest(NodePtr child_dest) final;
  bool CanAcceptSource(NodePtr src) const final;

  const zx_koid_t reference_clock_koid_;
  const std::shared_ptr<StreamSinkServer> stream_sink_server_;
  const DetachedThreadPtr detached_thread_;

  int64_t num_links_ = 0;
  std::unordered_map<NodePtr, std::shared_ptr<CommandQueue>> command_queues_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_STREAM_SINK_PRODUCER_NODE_H_
