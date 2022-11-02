// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/splitter_node.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <fbl/algorithm.h>

#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/common/memory_mapped_buffer.h"
#include "src/media/audio/services/mixer/fidl/ptr_decls.h"

namespace media_audio {

// static
std::shared_ptr<SplitterNode> SplitterNode::Create(Args args) {
  struct WithPublicCtor : public SplitterNode {
   public:
    explicit WithPublicCtor(Args args, std::shared_ptr<RingBuffer> ring_buffer)
        : SplitterNode(std::move(args), std::move(ring_buffer)) {}
  };

  // Default buffer size is one page.
  const auto ring_buffer_bytes = zx_system_get_page_size();
  const auto ring_buffer = std::make_shared<RingBuffer>(
      args.format, UnreadableClock(args.reference_clock),
      MemoryMappedBuffer::CreateOrDie(ring_buffer_bytes, /*writable=*/true));

  const auto splitter = std::make_shared<WithPublicCtor>(args, ring_buffer);

  const auto consumer_name = std::string(args.name) + ".Consumer";
  splitter->consumer_ = std::make_shared<ChildConsumerNode>(ChildConsumerNode::Args{
      .name = consumer_name,
      .format = args.format,
      .parent = splitter,
      .pipeline_stage = std::make_shared<SplitterConsumerStage>(SplitterConsumerStage::Args{
          .name = consumer_name,
          .format = args.format,
          .reference_clock = UnreadableClock(args.reference_clock),
          .ring_buffer = ring_buffer,
      }),
      .ring_buffer = ring_buffer,
      .ring_buffer_bytes = ring_buffer_bytes,
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
      .splitter_thread = consumer_->thread(),
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
           args.parent->pipeline_direction(), args.pipeline_stage, args.parent),
      format_(args.format),
      ring_buffer_(std::move(args.ring_buffer)),
      ring_buffer_bytes_(args.ring_buffer_bytes) {}

std::optional<std::pair<ThreadId, fit::closure>> SplitterNode::ChildConsumerNode::SetMaxDelays(
    Delays delays) {
  Node::SetMaxDelays(delays);

  // If any downstream delays have changed, recompute the ring buffer size. The ring buffer must be
  // large enough for all downstream output and input pipelines. See discussion in ../docs/delay.md.
  if (delays.downstream_output_pipeline_delay.has_value() ||
      delays.downstream_input_pipeline_delay.has_value()) {
    auto min_ring_buffer_bytes = format_.bytes_per(max_downstream_output_pipeline_delay() +
                                                   max_downstream_input_pipeline_delay());

    // Since VMOs are allocated in pages, round up to the page size.
    auto new_ring_buffer_bytes =
        fbl::round_up(static_cast<uint64_t>(min_ring_buffer_bytes), zx_system_get_page_size());

    // Allocate a new VMO if needed.
    if (new_ring_buffer_bytes > ring_buffer_bytes_) {
      ring_buffer_->SetBufferAsync(
          MemoryMappedBuffer::CreateOrDie(new_ring_buffer_bytes, /*writable=*/true));
      ring_buffer_bytes_ = new_ring_buffer_bytes;
    }
  }

  // If max_downstream_output_pipeline_delay changed, return a closure to notify the consumer stage.
  if (delays.downstream_output_pipeline_delay.has_value()) {
    return std::make_pair(thread()->id(), [stage = pipeline_stage(),
                                           delay = max_downstream_output_pipeline_delay()]() {
      ScopedThreadChecker checker(stage->thread()->checker());
      stage->set_max_downstream_output_pipeline_delay(delay);
    });
  }

  return std::nullopt;
}

zx::duration SplitterNode::ChildConsumerNode::PresentationDelayForSourceEdge(
    const Node* source) const {
  // Delays, if any, are accounted for by the child producer nodes.
  return zx::nsec(0);
}

SplitterNode::ChildProducerNode::ChildProducerNode(Args args)
    : Node(Type::kProducer, args.name, args.parent->reference_clock(),
           args.parent->pipeline_direction(), args.pipeline_stage, args.parent),
      splitter_thread_(args.splitter_thread) {}

zx::duration SplitterNode::ChildProducerNode::PresentationDelayForSourceEdge(
    const Node* source) const {
  FX_CHECK(!source);

  // Loopback edges have no delay.
  if (pipeline_direction() == PipelineDirection::kOutput && dest() &&
      dest()->pipeline_direction() == PipelineDirection::kInput) {
    return zx::nsec(0);
  }
  // Same-thread edges have no delay.
  if (thread() == splitter_thread_) {
    return zx::nsec(0);
  }
  // Otherwise, this is a cross-thread non-loopback edge. The delay is equivalent to the downstream
  // thread's mix period.
  return thread()->mix_period();
}

}  // namespace media_audio
