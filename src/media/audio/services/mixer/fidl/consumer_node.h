// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_CONSUMER_NODE_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_CONSUMER_NODE_H_

#include <zircon/types.h>

#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/fidl/delay_watcher_client.h"
#include "src/media/audio/services/mixer/fidl/graph_mix_thread.h"
#include "src/media/audio/services/mixer/fidl/node.h"
#include "src/media/audio/services/mixer/mix/consumer_stage.h"

namespace media_audio {

// This is an ordinary node that wraps a ConsumerStage.
class ConsumerNode : public Node {
 public:
  struct Args {
    // Name of this node.
    std::string_view name;

    // Whether this node participates in an input pipeline or an output pipeline.
    PipelineDirection pipeline_direction;

    // Format of audio consumed by this node.
    Format format;

    // Reference clock used by this consumer.
    std::shared_ptr<Clock> reference_clock;

    // Ticks of media time per nanoseconds of reference time.
    TimelineRate media_ticks_per_ns;

    // How to write all consumed packets.
    std::shared_ptr<ConsumerStage::Writer> writer;

    // Which thread the consumer is assigned to.
    std::shared_ptr<GraphMixThread> thread;

    // For output pipelines, the downstream delay at this consumer.
    std::shared_ptr<DelayWatcherClient> delay_watcher;

    // For queuing tasks on mixer threads.
    std::shared_ptr<GlobalTaskQueue> global_task_queue;
  };

  static std::shared_ptr<ConsumerNode> Create(Args args);

  // Starts this consumer.
  void Start(ConsumerStage::StartCommand cmd) const;

  // Stops this consumer.
  void Stop(ConsumerStage::StopCommand cmd) const;

  // Implements `Node`.
  std::optional<std::pair<ThreadId, fit::closure>> SetMaxDelays(Delays delays) final;
  zx::duration PresentationDelayForSourceEdge(const Node* source) const final;

 private:
  using PendingStartStopCommand = ConsumerStage::PendingStartStopCommand;

  ConsumerNode(std::string_view name, std::shared_ptr<Clock> reference_clock,
               PipelineDirection pipeline_direction, ConsumerStagePtr pipeline_stage,
               const Format& format,
               std::shared_ptr<PendingStartStopCommand> pending_start_stop_command,
               std::shared_ptr<GraphMixThread> mix_thread,
               std::shared_ptr<DelayWatcherClient> delay_watcher);

  NodePtr CreateNewChildSource() final {
    UNREACHABLE << "CreateNewChildSource should not be called on ordinary nodes";
  }
  NodePtr CreateNewChildDest() final {
    UNREACHABLE << "CreateNewChildDest should not be called on ordinary nodes";
  }
  void PrepareToDeleteSelf() final;
  bool CanAcceptSourceFormat(const Format& format) const final;
  std::optional<size_t> MaxSources() const final;
  bool AllowsDest() const final;

  const Format format_;
  const std::shared_ptr<PendingStartStopCommand> pending_start_stop_command_;
  const std::shared_ptr<GraphMixThread> mix_thread_;
  const ConsumerStagePtr consumer_stage_;

  // Logically const, but non-const so we can discard this in `PrepareToDeleteSelf` to remove a
  // circular reference.
  std::shared_ptr<DelayWatcherClient> delay_watcher_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_CONSUMER_NODE_H_
