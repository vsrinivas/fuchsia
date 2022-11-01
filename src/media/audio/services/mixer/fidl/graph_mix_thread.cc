// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/graph_mix_thread.h"

#include <lib/syslog/cpp/macros.h>

#include <memory>

#include "src/media/audio/lib/clock/clock.h"
#include "src/media/audio/services/mixer/mix/consumer_stage.h"

namespace media_audio {

GraphMixThread::GraphMixThread(PipelineMixThread::Args args)
    : GraphThread(args.global_task_queue), thread_(PipelineMixThread::Create(std::move(args))) {}

GraphMixThread::GraphMixThread(std::shared_ptr<GlobalTaskQueue> global_task_queue,
                               std::shared_ptr<PipelineMixThread> pipeline_thread)
    : GraphThread(std::move(global_task_queue)), thread_(std::move(pipeline_thread)) {}

void GraphMixThread::IncrementClockUsage(std::shared_ptr<Clock> clock) {
  const auto [it, is_new] = clock_usages_.try_emplace(std::move(clock), 1);
  if (is_new) {
    // Forward to the `PipelineMixThread`.
    PushTask([pipeline_thread = thread_, clock = it->first]() mutable {
      ScopedThreadChecker checker(pipeline_thread->checker());
      pipeline_thread->AddClock(std::move(clock));
    });
  } else {
    ++it->second;
  }
}
void GraphMixThread::DecrementClockUsage(std::shared_ptr<Clock> clock) {
  if (const auto it = clock_usages_.find(clock); it != clock_usages_.end() && --it->second == 0) {
    // Forward to the `PipelineMixThread`.
    PushTask([pipeline_thread = thread_, clock = std::move(clock)]() mutable {
      ScopedThreadChecker checker(pipeline_thread->checker());
      pipeline_thread->RemoveClock(std::move(clock));
    });
    clock_usages_.erase(it);
  }
}

void GraphMixThread::AddConsumer(ConsumerStagePtr consumer_stage) {
  FX_CHECK(consumers_.count(consumer_stage) == 0)
      << "cannot add Consumer twice: " << consumer_stage->name();

  consumers_.insert(consumer_stage);
  // Forward to the `PipelineMixThread`.
  PushTask([pipeline_thread = thread_, consumer_stage = std::move(consumer_stage)]() mutable {
    ScopedThreadChecker checker(pipeline_thread->checker());
    pipeline_thread->AddConsumer(std::move(consumer_stage));
  });
}

void GraphMixThread::RemoveConsumer(ConsumerStagePtr consumer_stage) {
  FX_CHECK(consumers_.count(consumer_stage) == 1)
      << "cannot find Consumer to remove: " << consumer_stage->name();

  consumers_.erase(consumer_stage);
  // Forward to the `PipelineMixThread`.
  PushTask([pipeline_thread = thread_, consumer_stage = std::move(consumer_stage)]() mutable {
    ScopedThreadChecker checker(pipeline_thread->checker());
    pipeline_thread->RemoveConsumer(std::move(consumer_stage));
  });
}

void GraphMixThread::NotifyConsumerStarting(ConsumerStagePtr consumer_stage) {
  FX_CHECK(consumers_.count(consumer_stage) == 1)
      << "cannot find Consumer to notify: " << consumer_stage->name();

  // Forward to the PipelineMixThread.
  PushTask([pipeline_thread = thread_, consumer_stage]() {
    ScopedThreadChecker checker(pipeline_thread->checker());
    pipeline_thread->NotifyConsumerStarting(consumer_stage);
  });
}

void GraphMixThread::Shutdown() {
  // Forward to the `PipelineMixThread`.
  PushTask([pipeline_thread = thread_]() {
    ScopedThreadChecker checker(pipeline_thread->checker());
    pipeline_thread->Shutdown();
  });
}

}  // namespace media_audio
