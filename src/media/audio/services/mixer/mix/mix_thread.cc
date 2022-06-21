// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/mix_thread.h"

#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/thread.h>

#include <thread>

namespace media_audio {

MixThreadPtr MixThread::Create(ThreadId id,
                               fuchsia_audio_mixer::wire::GraphCreateThreadRequest& options,
                               std::shared_ptr<GlobalTaskQueue> global_task_queue,
                               std::shared_ptr<Timer> timer) {
  // std::make_shared requires a public ctor, but we hide our ctor to force callers to use Create.
  struct WithPublicCtor : public MixThread {
    WithPublicCtor(ThreadId id, fuchsia_audio_mixer::wire::GraphCreateThreadRequest& options,
                   std::shared_ptr<GlobalTaskQueue> global_task_queue, std::shared_ptr<Timer> timer)
        : MixThread(id, options, std::move(global_task_queue), std::move(timer)) {}
  };

  MixThreadPtr thread =
      std::make_shared<WithPublicCtor>(id, options, std::move(global_task_queue), std::move(timer));

  // Force MixThread::Run to wait until private fields are fully initialized.
  std::lock_guard<std::mutex> startup_lock(thread->startup_mutex_);

  // Start the kernel thread. This can't happen in MixThread::MixThread because we want
  // the closures to hold ThreadPtrs, which we can't get until after the ctor.
  // Once the thread is started, we can detach and discard the std::thread.
  // Shutdown is async so we have no need to join.
  std::thread t(MixThread::Run, thread);
  t.detach();

  // Now that we have a std::thread, we can create the checker.
  thread->checker_ = std::make_unique<ThreadChecker>(t.get_id());

  return thread;
}

MixThread::MixThread(ThreadId id, fuchsia_audio_mixer::wire::GraphCreateThreadRequest& options,
                     std::shared_ptr<GlobalTaskQueue> global_task_queue,
                     std::shared_ptr<Timer> timer)
    : id_(id),
      name_(options.has_name() ? options.name().get() : ""),
      deadline_profile_(options.has_deadline_profile() ? std::move(options.deadline_profile())
                                                       : zx::profile()),
      global_task_queue_(std::move(global_task_queue)),
      timer_(std::move(timer)) {}

// static
void MixThread::Run(MixThreadPtr thread) {  // NOLINT(performance-unnecessary-value-param)
  if (thread->deadline_profile_) {
    if (auto status = zx::thread::self()->set_profile(thread->deadline_profile_, 0);
        status != ZX_OK) {
      FX_PLOGS(WARNING, status) << "Failed to set deadline profile for thread '" << thread->name()
                                << "'";
    }
  }

  // Wait until private fields are fully initialized.
  std::lock_guard<std::mutex> startup_lock(thread->startup_mutex_);

  FX_LOGS(INFO) << "MixThread starting: '" << thread->name() << "' (" << thread.get() << ")";
  thread->global_task_queue_->RegisterTimer(thread->id_, thread->timer_);

  auto cleanup = fit::defer([thread]() {
    FX_LOGS(INFO) << "MixThread stopping: '" << thread->name() << "' (" << thread.get() << ")";
    thread->global_task_queue_->UnregisterTimer(thread->id_);
  });

  thread->RunLoop();
}

void MixThread::RunLoop() {
  for (;;) {
    // TODO(fxbug.dev/87651): use the wake time for the next mix job
    auto wake_reason = timer_->SleepUntil(zx::time::infinite());
    if (wake_reason.shutdown_set) {
      return;
    }
    if (wake_reason.event_set) {
      // An "event" means tasks are available in the global task queue.
      global_task_queue_->RunForThread(id());
    }

    // TODO(fxbug.dev/87651): handle wake_reason.deadline_expired
  }
}

void MixThread::AddConsumer(ConsumerStagePtr consumer) { FX_CHECK(false) << "not implemented"; }

void MixThread::RemoveConsumer(ConsumerStagePtr consumer) { FX_CHECK(false) << "not implemented"; }

void MixThread::Shutdown() {
  // Run will exit the next time it wakes up.
  // Technically this is thread safe, but Shutdown is annotated with TA_REQ(checker())
  // anyway because it's simpler to say that all non-const methods are not thread safe.
  timer_->SetShutdownBit();
}

}  // namespace media_audio
