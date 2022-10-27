// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/producer_node.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/fidl/ptr_decls.h"
#include "src/media/audio/services/mixer/mix/simple_packet_queue_producer_stage.h"
#include "src/media/audio/services/mixer/mix/simple_ring_buffer_producer_stage.h"

namespace media_audio {

namespace {
constexpr size_t kStreamSinkServerIndex = 0;
constexpr size_t kRingBufferIndex = 1;
}  // namespace

// static
std::shared_ptr<ProducerNode> ProducerNode::Create(Args args) {
  struct WithPublicCtor : public ProducerNode {
   public:
    explicit WithPublicCtor(std::string_view name, std::shared_ptr<Clock> reference_clock,
                            PipelineDirection pipeline_direction, PipelineStagePtr pipeline_stage,
                            std::shared_ptr<PendingStartStopCommand> pending_start_stop_command)
        : ProducerNode(name, std::move(reference_clock), pipeline_direction,
                       std::move(pipeline_stage), std::move(pending_start_stop_command)) {}
  };

  const auto pending_start_stop_command = std::make_shared<PendingStartStopCommand>();
  PipelineStagePtr internal_source;

  if (args.data_source.index() == kStreamSinkServerIndex) {
    auto& server = std::get<kStreamSinkServerIndex>(args.data_source);
    FX_CHECK(server);
    FX_CHECK(args.format == server->format());

    internal_source =
        std::make_shared<SimplePacketQueueProducerStage>(SimplePacketQueueProducerStage::Args{
            .name = args.name,
            .format = args.format,
            .reference_clock = UnreadableClock(args.reference_clock),
            .command_queue = server->command_queue(),
        });
  } else {
    auto& rb = std::get<kRingBufferIndex>(args.data_source);
    FX_CHECK(rb);
    FX_CHECK(args.format == rb->format());
    FX_CHECK(args.reference_clock == rb->reference_clock());

    internal_source = std::make_shared<SimpleRingBufferProducerStage>(args.name, rb);
  }

  auto pipeline_stage = std::make_shared<ProducerStage>(ProducerStage::Args{
      .name = args.name,
      .format = args.format,
      .reference_clock = UnreadableClock(args.reference_clock),
      .media_ticks_per_ns = args.media_ticks_per_ns,
      .pending_start_stop_command = pending_start_stop_command,
      .internal_source = std::move(internal_source),
  });
  pipeline_stage->set_thread(args.detached_thread->pipeline_thread());

  auto node = std::make_shared<WithPublicCtor>(args.name, std::move(args.reference_clock),
                                               args.pipeline_direction, std::move(pipeline_stage),
                                               std::move(pending_start_stop_command));
  node->set_thread(args.detached_thread);
  return node;
}

ProducerNode::ProducerNode(std::string_view name, std::shared_ptr<Clock> reference_clock,
                           PipelineDirection pipeline_direction, PipelineStagePtr pipeline_stage,
                           std::shared_ptr<PendingStartStopCommand> pending_start_stop_command)
    : Node(Type::kProducer, name, std::move(reference_clock), pipeline_direction,
           std::move(pipeline_stage), /*parent=*/nullptr),
      pending_start_stop_command_(std::move(pending_start_stop_command)) {}

void ProducerNode::Start(ProducerStage::StartCommand cmd) const {
  if (auto old = pending_start_stop_command_->swap(std::move(cmd)); old) {
    StartStopControl::CancelCommand(*old);
  }
}

void ProducerNode::Stop(ProducerStage::StopCommand cmd) const {
  if (auto old = pending_start_stop_command_->swap(std::move(cmd)); old) {
    StartStopControl::CancelCommand(*old);
  }
}

zx::duration ProducerNode::PresentationDelayForSourceEdge(const Node* source) const {
  // Producers do not have internal delay contribution.
  // TODO(fxbug.dev/87651): Add a method to introduce external delay.
  return zx::duration(0);
}

bool ProducerNode::CanAcceptSourceFormat(const Format& format) const { return false; }
std::optional<size_t> ProducerNode::MaxSources() const { return 0; }
bool ProducerNode::AllowsDest() const { return true; }

}  // namespace media_audio
