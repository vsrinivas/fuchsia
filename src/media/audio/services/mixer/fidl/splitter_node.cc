// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/splitter_node.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/common/memory_mapped_buffer.h"
#include "src/media/audio/services/mixer/fidl/ptr_decls.h"

namespace media_audio {

// TODO(fxbug.dev/87651): Handle delay, including:
// - if producer has the same thread as consumer, no delay
// - if the producer has a different thread, delay is the producer's thread's mix period
// - the ring buffer must be resized when maximum downstream delay increases
// - output vs input pipelines

// static
std::shared_ptr<SplitterNode> SplitterNode::Create(Args args) {
  struct WithPublicCtor : public SplitterNode {
   public:
    explicit WithPublicCtor(Args args, std::shared_ptr<RingBuffer> ring_buffer)
        : SplitterNode(std::move(args), std::move(ring_buffer)) {}
  };

  // Default buffer size is one page.
  const auto ring_buffer = std::make_shared<RingBuffer>(
      args.format, UnreadableClock(args.reference_clock),
      MemoryMappedBuffer::CreateOrDie(zx_system_get_page_size(), /*writable=*/true));

  const auto splitter = std::make_shared<WithPublicCtor>(args, ring_buffer);

  const auto consumer_name = std::string(args.name) + ".Consumer";
  splitter->consumer_ = std::make_shared<ChildConsumerNode>(ChildConsumerNode::Args{
      .name = consumer_name,
      .parent = splitter,
      .pipeline_stage = std::make_shared<SplitterConsumerStage>(SplitterConsumerStage::Args{
          .name = consumer_name,
          .format = args.format,
          .reference_clock = UnreadableClock(args.reference_clock),
          .ring_buffer = ring_buffer,
      }),
  });
  splitter->consumer_->set_thread(args.consumer_thread);
  splitter->consumer_->pipeline_stage()->set_thread(args.consumer_thread->pipeline_thread());
  return splitter;
}

SplitterNode::SplitterNode(Args args, std::shared_ptr<RingBuffer> ring_buffer)
    : Node(Type::kMeta, args.name, args.reference_clock, args.pipeline_direction,
           /*pipeline_stage=*/nullptr, /*parent=*/nullptr),
      format_(args.format),
      detached_thread_(std::move(args.detached_thread)),
      ring_buffer_(std::move(ring_buffer)) {}

NodePtr SplitterNode::CreateNewChildSource() {
  // We can have at most one incoming edge, represented by a ChildConsumerNode. Rather than
  // constructing a new ChildConsumerNode for each edge, we construct `consumer_` once and
  // add/remove it from `child_sources` when an incoming edge is created/deleted.
  return child_sources().empty() ? consumer_ : nullptr;
}

NodePtr SplitterNode::CreateNewChildDest() {
  // There can be an unlimited number of outgoing edges.
  const auto id = num_producers_created_++;
  const auto producer_name = std::string(name()) + ".Producer" + std::to_string(id);
  const auto producer = std::make_shared<ChildProducerNode>(ChildProducerNode::Args{
      .name = producer_name,
      .parent = shared_from_this(),
      .pipeline_stage = std::make_shared<SplitterProducerStage>(SplitterProducerStage::Args{
          .name = producer_name,
          .format = format_,
          .reference_clock = UnreadableClock(reference_clock()),
          .ring_buffer = ring_buffer_,
          .consumer = consumer_->pipeline_stage(),
      }),
  });
  producer->set_thread(detached_thread_);
  producer->pipeline_stage()->set_thread(detached_thread_->pipeline_thread());
  return producer;
}

void SplitterNode::DestroySelf() {
  // Normally, to destroy a node, it's sufficient to delete all incoming and outgoing edges, since
  // deleting those edges will delete child nodes, removing circular child <-> parent references.
  // In this case we hold onto `consumer_` after edges are deleted, so it gets discarded manually.
  consumer_ = nullptr;
}

SplitterNode::ChildConsumerNode::ChildConsumerNode(Args args)
    : Node(Type::kConsumer, args.name, args.parent->reference_clock(),
           args.parent->pipeline_direction(), args.pipeline_stage, args.parent) {}

zx::duration SplitterNode::ChildConsumerNode::PresentationDelayForSourceEdge(
    const Node* source) const {
  // TODO(fxbug.dev/87651): implement
  return zx::nsec(0);
}

SplitterNode::ChildProducerNode::ChildProducerNode(Args args)
    : Node(Type::kProducer, args.name, args.parent->reference_clock(),
           args.parent->pipeline_direction(), args.pipeline_stage, args.parent) {}

zx::duration SplitterNode::ChildProducerNode::PresentationDelayForSourceEdge(
    const Node* source) const {
  // TODO(fxbug.dev/87651): implement
  return zx::nsec(0);
}

}  // namespace media_audio
