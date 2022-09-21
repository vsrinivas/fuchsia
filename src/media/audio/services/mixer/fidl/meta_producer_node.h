// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_META_PRODUCER_NODE_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_META_PRODUCER_NODE_H_

#include <lib/zx/time.h>

#include <unordered_map>
#include <variant>

#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/fidl/node.h"
#include "src/media/audio/services/mixer/fidl/ptr_decls.h"
#include "src/media/audio/services/mixer/fidl_realtime/stream_sink_server.h"
#include "src/media/audio/services/mixer/mix/producer_stage.h"
#include "src/media/audio/services/mixer/mix/ring_buffer.h"
#include "src/media/audio/services/mixer/mix/simple_packet_queue_producer_stage.h"

namespace media_audio {

// This meta node wraps a set of child ProducerNodes, all of which produce identical data.
class MetaProducerNode : public Node, public std::enable_shared_from_this<MetaProducerNode> {
 public:
  using DataSource = std::variant<std::shared_ptr<StreamSinkServer>, std::shared_ptr<RingBuffer>>;

  struct Args {
    // Name of this node.
    std::string_view name;

    // Whether this node participates in an input pipeline or an output pipeline.
    PipelineDirection pipeline_direction;

    // Format of data produced by this node.
    Format format;

    // Reference clock of this nodes's destination streams.
    zx_koid_t reference_clock_koid;

    // Object from which to produce data.
    DataSource data_source;

    // On creation, child nodes are initially assigned to this DetachedThread.
    DetachedThreadPtr detached_thread;
  };

  static std::shared_ptr<MetaProducerNode> Create(Args args);

  // Starts this producer. The command is forwarded to each outgoing command queue.
  void Start(ProducerStage::StartCommand cmd) const;

  // Stops this producer. The command is forwarded to each outgoing command queue.
  void Stop(ProducerStage::StopCommand cmd) const;

  // Implements `Node`.
  zx::duration GetSelfPresentationDelayForSource(const NodePtr& source) const final {
    UNREACHABLE << "GetSelfPresentationDelayForSource should not be called on meta nodes";
  }

 private:
  static inline constexpr size_t kStreamSinkServerIndex = 0;
  static inline constexpr size_t kRingBufferIndex = 1;

  using StartStopCommandQueue = ProducerStage::CommandQueue;
  using PacketCommandQueue = SimplePacketQueueProducerStage::CommandQueue;

  MetaProducerNode(Args args)
      : Node(args.name, /*is_meta=*/true, args.reference_clock_koid, args.pipeline_direction,
             /*pipeline_stage=*/nullptr,
             /*parent=*/nullptr),
        format_(args.format),
        data_source_(std::move(args.data_source)),
        detached_thread_(std::move(args.detached_thread)) {}

  NodePtr CreateNewChildSource() final;
  NodePtr CreateNewChildDest() final;
  void DestroyChildDest(NodePtr child_dest) final;

  bool CanAcceptSourceFormat(const Format& format) const final {
    UNREACHABLE << "CanAcceptSourceFormat should not be called on meta nodes";
  }
  std::optional<size_t> MaxSources() const final {
    UNREACHABLE << "MaxSources should not be called on meta nodes";
  }
  bool AllowsDest() const final { UNREACHABLE << "AllowsDest should not be called on meta nodes"; }

  const Format format_;
  const DataSource data_source_;
  const DetachedThreadPtr detached_thread_;

  int64_t num_links_ = 0;
  struct CommandQueues {
    std::shared_ptr<StartStopCommandQueue> start_stop;
    std::shared_ptr<PacketCommandQueue> packet;  // if DataSource is a StreamSinkServer
  };
  std::unordered_map<NodePtr, CommandQueues> command_queues_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_META_PRODUCER_NODE_H_
