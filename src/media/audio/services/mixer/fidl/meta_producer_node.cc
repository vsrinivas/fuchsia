// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/meta_producer_node.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/fidl/producer_node.h"
#include "src/media/audio/services/mixer/fidl/ptr_decls.h"
#include "src/media/audio/services/mixer/mix/simple_ring_buffer_producer_stage.h"

namespace media_audio {

// static
std::shared_ptr<MetaProducerNode> MetaProducerNode::Create(Args args) {
  struct WithPublicCtor : public MetaProducerNode {
   public:
    explicit WithPublicCtor(Args args) : MetaProducerNode(std::move(args)) {}
  };

  if (args.data_source.index() == kStreamSinkServerIndex) {
    auto& server = std::get<kStreamSinkServerIndex>(args.data_source);
    FX_CHECK(server);
    FX_CHECK(args.format == server->format());
  } else {
    auto& rb = std::get<kRingBufferIndex>(args.data_source);
    FX_CHECK(rb);
    FX_CHECK(args.format == rb->format());
    FX_CHECK(args.reference_clock == rb->reference_clock());
  }

  return std::make_shared<WithPublicCtor>(std::move(args));
}

void MetaProducerNode::Start(ProducerStage::StartCommand cmd) const {
  for (auto& [node, queues] : command_queues_) {
    queues.start_stop->push(cmd);
  }
}

void MetaProducerNode::Stop(ProducerStage::StopCommand cmd) const {
  for (auto& [node, queues] : command_queues_) {
    queues.start_stop->push(cmd);
  }
}

NodePtr MetaProducerNode::CreateNewChildSource() {
  // Producers do not have source nodes.
  return nullptr;
}

NodePtr MetaProducerNode::CreateNewChildDest() {
  CommandQueues queues{.start_stop = std::make_shared<StartStopCommandQueue>()};
  std::string child_name = std::string(name()) + "@" + std::to_string(num_links_++);

  PipelineStagePtr internal_source;

  if (data_source_.index() == kStreamSinkServerIndex) {
    queues.packet = std::make_shared<PacketCommandQueue>();

    // Attach the writer end.
    auto& server = std::get<kStreamSinkServerIndex>(data_source_);
    server->thread().PostTask([server, packet_queue = queues.packet]() {
      ScopedThreadChecker checker(server->thread().checker());
      server->AddProducerQueue(packet_queue);
    });

    internal_source =
        std::make_shared<SimplePacketQueueProducerStage>(SimplePacketQueueProducerStage::Args{
            .name = child_name,
            .format = format_,
            .reference_clock = UnreadableClock(reference_clock()),
            .command_queue = queues.packet,
        });
  } else {
    auto& rb = std::get<kRingBufferIndex>(data_source_);
    internal_source = std::make_shared<SimpleRingBufferProducerStage>(child_name, rb);
  }

  auto child = ProducerNode::Create({
      .name = child_name,
      .reference_clock = reference_clock(),
      .pipeline_direction = pipeline_direction(),
      .parent = shared_from_this(),
      .start_stop_command_queue = queues.start_stop,
      .internal_source = std::move(internal_source),
      .detached_thread = detached_thread_,
  });

  command_queues_[child] = std::move(queues);
  return child;
}

void MetaProducerNode::DestroyChildDest(NodePtr child_dest) {
  auto it = command_queues_.find(child_dest);
  FX_CHECK(it != command_queues_.end());

  if (data_source_.index() == kStreamSinkServerIndex) {
    auto& server = std::get<kStreamSinkServerIndex>(data_source_);
    server->thread().PostTask([server, packet_queue = it->second.packet]() {
      ScopedThreadChecker checker(server->thread().checker());
      server->RemoveProducerQueue(packet_queue);
    });
  }

  command_queues_.erase(it);
}

}  // namespace media_audio
