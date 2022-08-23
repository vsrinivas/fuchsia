// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/packet_queue_producer_node.h"

#include <lib/syslog/cpp/macros.h>

namespace media_audio {

// static
std::shared_ptr<PacketQueueProducerNode> PacketQueueProducerNode::Create(Args args) {
  struct WithPublicCtor : public PacketQueueProducerNode {
   public:
    explicit WithPublicCtor(std::string_view name, PipelineStagePtr pipeline_stage, NodePtr parent)
        : PacketQueueProducerNode(name, std::move(pipeline_stage), std::move(parent)) {}
  };

  auto pipeline_stage = std::make_shared<PacketQueueProducerStage>(PacketQueueProducerStage::Args{
      .name = args.name,
      .format = args.format,
      .reference_clock_koid = args.reference_clock_koid,
      .command_queue = std::move(args.command_queue),
  });
  pipeline_stage->set_thread(args.detached_thread);

  auto node = std::make_shared<WithPublicCtor>(args.name, std::move(pipeline_stage),
                                               std::move(args.parent));
  node->set_pipeline_stage_thread(args.detached_thread);
  return node;
}

NodePtr PacketQueueProducerNode::CreateNewChildSource() {
  // CreateNewChildSource should not be called on ordinary nodes.
  FX_CHECK(false);
  __builtin_unreachable();
}

NodePtr PacketQueueProducerNode::CreateNewChildDest() {
  // CreateNewChildDest should not be called on ordinary nodes.
  FX_CHECK(false);
  __builtin_unreachable();
}

bool PacketQueueProducerNode::CanAcceptSource(NodePtr src) const {
  // Producers do not have sources.
  return false;
}

}  // namespace media_audio
