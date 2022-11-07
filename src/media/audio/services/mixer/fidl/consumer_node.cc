// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/consumer_node.h"

#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/services/common/logging.h"

namespace media_audio {

// static
std::shared_ptr<ConsumerNode> ConsumerNode::Create(Args args) {
  struct WithPublicCtor : public ConsumerNode {
   public:
    explicit WithPublicCtor(std::string_view name, std::shared_ptr<Clock> reference_clock,
                            PipelineDirection pipeline_direction, ConsumerStagePtr pipeline_stage,
                            const Format& format,
                            std::shared_ptr<PendingStartStopCommand> pending_start_stop_command,
                            std::shared_ptr<GraphMixThread> mix_thread,
                            std::shared_ptr<DelayWatcherClient> delay_watcher)
        : ConsumerNode(name, std::move(reference_clock), pipeline_direction,
                       std::move(pipeline_stage), format, std::move(pending_start_stop_command),
                       std::move(mix_thread), std::move(delay_watcher)) {}
  };

  auto pending_start_stop_command = std::make_shared<PendingStartStopCommand>();
  auto pipeline_stage = std::make_shared<ConsumerStage>(ConsumerStage::Args{
      .name = args.name,
      .pipeline_direction = args.pipeline_direction,
      .format = args.format,
      .reference_clock = UnreadableClock(args.reference_clock),
      .media_ticks_per_ns = args.media_ticks_per_ns,
      .pending_start_stop_command = pending_start_stop_command,
      .writer = std::move(args.writer),
  });
  pipeline_stage->set_thread(args.thread->pipeline_thread());

  auto node = std::make_shared<WithPublicCtor>(
      args.name, std::move(args.reference_clock), args.pipeline_direction, pipeline_stage,
      args.format, std::move(pending_start_stop_command), args.thread, args.delay_watcher);

  if (args.pipeline_direction == PipelineDirection::kOutput) {
    FX_CHECK(args.delay_watcher);
    args.delay_watcher->SetCallback([node, global_task_queue = std::move(args.global_task_queue)](
                                        auto delay) mutable {
      if (auto result = node->SetMaxDelays({.downstream_output_pipeline_delay = delay}); result) {
        global_task_queue->Push(result->first, std::move(result->second));
      }
    });
  }

  // Now that the consumer has been fully initialized, add it to the mix thread.
  args.thread->AddConsumer(pipeline_stage);

  return node;
}

ConsumerNode::ConsumerNode(std::string_view name, std::shared_ptr<Clock> reference_clock,
                           PipelineDirection pipeline_direction, ConsumerStagePtr pipeline_stage,
                           const Format& format,
                           std::shared_ptr<PendingStartStopCommand> pending_start_stop_command,
                           std::shared_ptr<GraphMixThread> mix_thread,
                           std::shared_ptr<DelayWatcherClient> delay_watcher)
    : Node(Type::kConsumer, name, std::move(reference_clock), pipeline_direction, pipeline_stage,
           /*parent=*/nullptr),
      format_(format),
      pending_start_stop_command_(std::move(pending_start_stop_command)),
      mix_thread_(std::move(mix_thread)),
      consumer_stage_(std::move(pipeline_stage)),
      delay_watcher_(std::move(delay_watcher)) {
  set_thread(mix_thread_);
}

void ConsumerNode::Start(ConsumerStage::StartCommand cmd) const {
  if (auto old = pending_start_stop_command_->swap(std::move(cmd)); old) {
    StartStopControl::CancelCommand(*old);
  } else {
    mix_thread_->NotifyConsumerStarting(consumer_stage_);
  }
}

void ConsumerNode::Stop(ConsumerStage::StopCommand cmd) const {
  if (auto old = pending_start_stop_command_->swap(std::move(cmd)); old) {
    StartStopControl::CancelCommand(*old);
  } else {
    mix_thread_->NotifyConsumerStarting(consumer_stage_);
  }
}

std::optional<std::pair<ThreadId, fit::closure>> ConsumerNode::SetMaxDelays(Delays delays) {
  Node::SetMaxDelays(delays);

  // When either of these fields change, notify our ConsumerStage. At most one of these can change
  // at a time (since they are defined on only output and input pipelines, respectively), hence we
  // don't need to merge the closures.
  if (delays.downstream_output_pipeline_delay.has_value()) {
    FX_CHECK(pipeline_direction() == PipelineDirection::kOutput);
    return std::make_pair(thread()->id(), [consumer_stage = consumer_stage_,
                                           delay = *delays.downstream_output_pipeline_delay]() {
      consumer_stage->set_downstream_delay(delay);
    });
  }
  if (delays.upstream_input_pipeline_delay.has_value()) {
    FX_CHECK(pipeline_direction() == PipelineDirection::kInput);
    // ConsumerStage::upstream_delay_for_source does not include delay added by this ConsumerNode.
    auto delay = *delays.upstream_input_pipeline_delay;
    if (!sources().empty()) {
      FX_CHECK(sources().size() == 1);
      delay -= PresentationDelayForSourceEdge(sources()[0].get());
    }
    return std::make_pair(thread()->id(), [consumer_stage = consumer_stage_, delay]() {
      consumer_stage->set_upstream_delay_for_source(delay);
    });
  }

  return std::nullopt;
}

zx::duration ConsumerNode::PresentationDelayForSourceEdge(const Node* source) const {
  // Consumers add two mix periods worth of delay: Output pipelines operate one mix period in the
  // future, while input pipelines operate one period in the past, hence one period of delay. Plus,
  // each mix job might take up to one mix period to complete, hence one additional period of delay.
  return 2 * mix_thread_->mix_period();
}

void ConsumerNode::PrepareToDeleteSelf() {
  // Deregister from the thread.
  mix_thread_->RemoveConsumer(consumer_stage_);
  // Drop this to break a circular reference via delay_watcher_->SetCallback.
  delay_watcher_ = nullptr;
}

bool ConsumerNode::CanAcceptSourceFormat(const Format& format) const { return format == format_; }
std::optional<size_t> ConsumerNode::MaxSources() const { return 1; }
bool ConsumerNode::AllowsDest() const { return false; }

}  // namespace media_audio
