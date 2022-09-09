// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/stream_sink_producer_node.h"

#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/fidl/packet_queue_producer_node.h"

namespace media_audio {

// static
std::shared_ptr<StreamSinkProducerNode> StreamSinkProducerNode::Create(Args args) {
  struct WithPublicCtor : public StreamSinkProducerNode {
   public:
    explicit WithPublicCtor(Args args) : StreamSinkProducerNode(std::move(args)) {}
  };

  return std::make_shared<WithPublicCtor>(std::move(args));
}

void StreamSinkProducerNode::Start(PacketQueueProducerStage::StartCommand cmd) const {
  for (auto& [node, queue] : command_queues_) {
    queue->push(cmd);
  }
}

void StreamSinkProducerNode::Stop(PacketQueueProducerStage::StopCommand cmd) const {
  for (auto& [node, queue] : command_queues_) {
    queue->push(cmd);
  }
}

NodePtr StreamSinkProducerNode::CreateNewChildSource() {
  // Producers do not have source nodes.
  return nullptr;
}

NodePtr StreamSinkProducerNode::CreateNewChildDest() {
  auto command_queue = std::make_shared<CommandQueue>();

  // Attach the writer end.
  stream_sink_server_->thread().PostTask([server = stream_sink_server_, command_queue]() {
    ScopedThreadChecker checker(server->thread().checker());
    server->AddProducerQueue(command_queue);
  });

  // Create the reader end.
  auto node = PacketQueueProducerNode::Create({
      .name = std::string(name()) + "-Link" + std::to_string(num_links_++),
      .parent = shared_from_this(),
      .format = stream_sink_server_->format(),
      .reference_clock_koid = reference_clock_koid_,
      .command_queue = command_queue,
      .detached_thread = detached_thread_,
  });

  command_queues_[node] = command_queue;
  return node;
}

void StreamSinkProducerNode::DestroyChildDest(NodePtr child_dest) {
  auto it = command_queues_.find(child_dest);
  FX_CHECK(it != command_queues_.end());

  auto command_queue = it->second;
  stream_sink_server_->thread().PostTask([server = stream_sink_server_, command_queue]() {
    ScopedThreadChecker checker(server->thread().checker());
    server->RemoveProducerQueue(command_queue);
  });

  command_queues_.erase(it);
}

bool StreamSinkProducerNode::CanAcceptSource(NodePtr src) const {
  UNREACHABLE << "CanAcceptSource should not be called on meta nodes";
}

}  // namespace media_audio
