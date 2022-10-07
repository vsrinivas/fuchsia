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
                            std::shared_ptr<StartStopCommandQueue> start_stop_command_queue)
        : ProducerNode(name, std::move(reference_clock), pipeline_direction,
                       std::move(pipeline_stage), std::move(start_stop_command_queue)) {}
  };

  const auto start_stop_command_queue = std::make_shared<StartStopCommandQueue>();
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
      .command_queue = start_stop_command_queue,
      .internal_source = std::move(internal_source),
  });
  pipeline_stage->set_thread(args.detached_thread->pipeline_thread());

  auto node = std::make_shared<WithPublicCtor>(args.name, std::move(args.reference_clock),
                                               args.pipeline_direction, std::move(pipeline_stage),
                                               std::move(start_stop_command_queue));
  node->set_thread(args.detached_thread);
  return node;
}

ProducerNode::ProducerNode(std::string_view name, std::shared_ptr<Clock> reference_clock,
                           PipelineDirection pipeline_direction, PipelineStagePtr pipeline_stage,
                           std::shared_ptr<StartStopCommandQueue> start_stop_command_queue)
    : Node(Type::kProducer, name, std::move(reference_clock), pipeline_direction,
           std::move(pipeline_stage), /*parent=*/nullptr),
      start_stop_command_queue_(std::move(start_stop_command_queue)) {}

void ProducerNode::Start(ProducerStage::StartCommand cmd) const {
  start_stop_command_queue_->push(cmd);
}

void ProducerNode::Stop(ProducerStage::StopCommand cmd) const {
  start_stop_command_queue_->push(cmd);
}

zx::duration ProducerNode::GetSelfPresentationDelayForSource(const Node* source) const {
  // Producers do not have internal delay contribution.
  // TODO(fxbug.dev/87651): Add a method to introduce external delay.
  return zx::duration(0);
}

bool ProducerNode::CanAcceptSourceFormat(const Format& format) const { return false; }
std::optional<size_t> ProducerNode::MaxSources() const { return 0; }
bool ProducerNode::AllowsDest() const { return true; }

}  // namespace media_audio
