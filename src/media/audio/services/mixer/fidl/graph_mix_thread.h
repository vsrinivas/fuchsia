// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_GRAPH_MIX_THREAD_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_GRAPH_MIX_THREAD_H_

#include <lib/zx/time.h>

#include <memory>
#include <unordered_set>

#include "src/media/audio/lib/clock/clock.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/common/global_task_queue.h"
#include "src/media/audio/services/mixer/fidl/graph_thread.h"
#include "src/media/audio/services/mixer/mix/pipeline_mix_thread.h"

namespace media_audio {

// Wraps a PipelineMixThread. Updates to a GraphMixThread are eventually applied to the underlying
// PipelineMixThread, via a GlobalTaskQueue.
class GraphMixThread : public GraphThread {
 public:
  explicit GraphMixThread(PipelineMixThread::Args args);

  // Implements `GraphThread`.
  std::shared_ptr<PipelineThread> pipeline_thread() const final { return thread_; }
  void IncrementClockUsage(std::shared_ptr<Clock> clock) final;
  void DecrementClockUsage(std::shared_ptr<Clock> clock) final;

  // Reports the mix period.
  zx::duration mix_period() const { return thread_->mix_period(); }

  // Reports the number of consumers using this thread.
  int64_t num_consumers() const { return static_cast<int64_t>(consumers_.size()); }

  // These methods are forwarded asynchronously to the underlying PipelineMixThread.
  void AddConsumer(ConsumerStagePtr consumer_stage);
  void RemoveConsumer(ConsumerStagePtr consumer_stage);
  void NotifyConsumerStarting(ConsumerStagePtr consumer_stage);
  void Shutdown();

 private:
  // For testing: allow creating a GraphMixThread which wraps an arbitrary PipelineMixThread.
  friend std::shared_ptr<GraphMixThread> CreateGraphMixThreadWithoutLoop(
      PipelineMixThread::Args args);
  GraphMixThread(std::shared_ptr<GlobalTaskQueue> global_task_queue,
                 std::shared_ptr<PipelineMixThread> pipeline_thread);

  const std::shared_ptr<PipelineMixThread> thread_;

  std::unordered_map<std::shared_ptr<Clock>, int> clock_usages_;
  std::unordered_set<ConsumerStagePtr> consumers_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_GRAPH_MIX_THREAD_H_
