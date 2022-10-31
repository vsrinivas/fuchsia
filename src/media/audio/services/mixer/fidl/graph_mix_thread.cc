// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/graph_mix_thread.h"

#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/services/mixer/mix/consumer_stage.h"

namespace media_audio {

GraphMixThread::GraphMixThread(PipelineMixThread::Args args)
    : GraphThread(args.global_task_queue), thread_(PipelineMixThread::Create(std::move(args))) {}

GraphMixThread::GraphMixThread(std::shared_ptr<GlobalTaskQueue> q,
                               std::shared_ptr<PipelineMixThread> t)
    : GraphThread(std::move(q)), thread_(std::move(t)) {}

void GraphMixThread::AddConsumer(ConsumerStagePtr consumer_stage) {
  FX_CHECK(consumers_.count(consumer_stage) == 0)
      << "cannot add Consumer twice: " << consumer_stage->name();

  consumers_.insert(consumer_stage);

  // Forward to the PipelineMixThread.
  PushTask([pipeline_thread = thread_, consumer_stage]() {
    ScopedThreadChecker checker(pipeline_thread->checker());
    pipeline_thread->AddConsumer(consumer_stage);
    // TODO(fxbug.dev/87651): mix_thread->AddClock?
  });
}

void GraphMixThread::RemoveConsumer(ConsumerStagePtr consumer_stage) {
  FX_CHECK(consumers_.count(consumer_stage) == 1)
      << "cannot find Consumer to remove: " << consumer_stage->name();

  consumers_.erase(consumer_stage);

  // Forward to the PipelineMixThread.
  PushTask([pipeline_thread = thread_, consumer_stage]() {
    ScopedThreadChecker checker(pipeline_thread->checker());
    pipeline_thread->RemoveConsumer(consumer_stage);
    // TODO(fxbug.dev/87651): mix_thread->RemoveClock?
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
  // Forward to the PipelineMixThread.
  PushTask([pipeline_thread = thread_]() {
    ScopedThreadChecker checker(pipeline_thread->checker());
    pipeline_thread->Shutdown();
  });
}

}  // namespace media_audio
