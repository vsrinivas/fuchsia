// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/producer_node.h"

#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/fidl/ptr_decls.h"
#include "src/media/audio/services/mixer/fidl/reachability.h"
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
                            std::shared_ptr<PendingStartStopCommand> pending_start_stop_command,
                            std::shared_ptr<DelayWatcherClient> delay_watcher,
                            std::shared_ptr<DelayWatcherServerGroup> delay_reporter,
                            std::shared_ptr<GlobalTaskQueue> global_task_queue)
        : ProducerNode(name, std::move(reference_clock), pipeline_direction,
                       std::move(pipeline_stage), std::move(pending_start_stop_command),
                       std::move(delay_watcher), std::move(delay_reporter),
                       std::move(global_task_queue)) {}
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
            .initial_thread = args.detached_thread->pipeline_thread(),
            .command_queue = server->command_queue(),
        });
  } else {
    auto& rb = std::get<kRingBufferIndex>(args.data_source);
    FX_CHECK(rb);
    FX_CHECK(args.format == rb->format());
    FX_CHECK(args.reference_clock == rb->reference_clock());

    internal_source = std::make_shared<SimpleRingBufferProducerStage>(
        args.name, rb, args.detached_thread->pipeline_thread());
  }

  auto pipeline_stage = std::make_shared<ProducerStage>(ProducerStage::Args{
      .name = args.name,
      .format = args.format,
      .reference_clock = UnreadableClock(args.reference_clock),
      .media_ticks_per_ns = args.media_ticks_per_ns,
      .pending_start_stop_command = pending_start_stop_command,
      .internal_source = std::move(internal_source),
  });

  // In output pipelines, report downstream delay changes.
  std::shared_ptr<DelayWatcherServerGroup> delay_reporter;
  if (args.pipeline_direction == PipelineDirection::kOutput) {
    FX_CHECK(args.thread_for_lead_time_servers);
    delay_reporter = std::make_shared<DelayWatcherServerGroup>(
        std::string(args.name) + ".LeadTimeWatcher", std::move(args.thread_for_lead_time_servers));
  }

  auto node = std::make_shared<WithPublicCtor>(
      args.name, std::move(args.reference_clock), args.pipeline_direction,
      std::move(pipeline_stage), std::move(pending_start_stop_command),
      std::move(args.delay_watcher), std::move(delay_reporter), std::move(args.global_task_queue));
  node->set_thread(args.detached_thread);

  // In input pipelines, watch for upstream (external) delay changes.
  if (node->pipeline_direction() == PipelineDirection::kInput) {
    FX_CHECK(node->delay_watcher_);
    node->delay_watcher_->SetCallback(
        [node](std::optional<zx::duration> delay) { node->SetUpstreamInputDelay(delay); });
  } else {
    FX_CHECK(!node->delay_watcher_);
  }
  return node;
}

ProducerNode::ProducerNode(std::string_view name, std::shared_ptr<Clock> reference_clock,
                           PipelineDirection pipeline_direction, PipelineStagePtr pipeline_stage,
                           std::shared_ptr<PendingStartStopCommand> pending_start_stop_command,
                           std::shared_ptr<DelayWatcherClient> delay_watcher,
                           std::shared_ptr<DelayWatcherServerGroup> delay_reporter,
                           std::shared_ptr<GlobalTaskQueue> global_task_queue)
    : Node(Type::kProducer, name, std::move(reference_clock), pipeline_direction,
           std::move(pipeline_stage), /*parent=*/nullptr),
      pending_start_stop_command_(std::move(pending_start_stop_command)),
      global_task_queue_(std::move(global_task_queue)),
      delay_reporter_(std::move(delay_reporter)),
      delay_watcher_(std::move(delay_watcher)) {}

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

void ProducerNode::BindLeadTimeWatcher(fidl::ServerEnd<fuchsia_audio::DelayWatcher> server_end) {
  FX_CHECK(pipeline_direction() == PipelineDirection::kOutput);
  delay_reporter_->Add(std::move(server_end));
}

std::optional<std::pair<ThreadId, fit::closure>> ProducerNode::SetMaxDelays(Delays delays) {
  Node::SetMaxDelays(delays);

  // This value can only be changed by our delay_watcher_, which routes changes to
  // SetUpstreamInputDelay, which calls Node::SetMaxDelays.
  FX_CHECK(!delays.upstream_input_pipeline_delay.has_value());

  // When this field changes, report to all registered watchers.
  if (delays.downstream_output_pipeline_delay.has_value()) {
    FX_CHECK(pipeline_direction() == PipelineDirection::kOutput);
    delay_reporter_->set_delay(*delays.downstream_output_pipeline_delay);
  }

  return std::nullopt;
}

zx::duration ProducerNode::PresentationDelayForSourceEdge(const Node* source) const {
  // Source delay comes from upstream. This is defined for input pipelines only.
  if (pipeline_direction() == PipelineDirection::kInput) {
    return upstream_input_delay_;
  }
  return zx::nsec(0);
}

void ProducerNode::PrepareToDeleteSelf() {
  if (pipeline_direction() == PipelineDirection::kInput) {
    // Drop this to break a circular reference via delay_watcher_->SetCallback.
    delay_watcher_ = nullptr;
  } else {
    delay_reporter_->Shutdown();
  }
}

bool ProducerNode::CanAcceptSourceFormat(const Format& format) const { return false; }
std::optional<size_t> ProducerNode::MaxSources() const { return 0; }
bool ProducerNode::AllowsDest() const { return true; }

void ProducerNode::SetUpstreamInputDelay(std::optional<zx::duration> delay) {
  // If the delay is unknown, assume zero.
  upstream_input_delay_ = delay ? *delay : zx::nsec(0);
  Node::SetMaxDelays({.upstream_input_pipeline_delay = upstream_input_delay_});

  // Recompute at our destination node.
  if (!dest()) {
    return;
  }

  std::map<ThreadId, std::vector<fit::closure>> closures;
  RecomputeMaxUpstreamDelays(*dest(), closures);

  for (auto& [thread_id, closures_for_thread] : closures) {
    global_task_queue_->Push(thread_id, [closures_for_thread = std::move(closures_for_thread)]() {
      for (auto& fn : closures_for_thread) {
        FX_CHECK(fn);
        fn();
      }
    });
  }
}

}  // namespace media_audio
