// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_MIX_THREAD_H_
#define SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_MIX_THREAD_H_

#include <fidl/fuchsia.audio.mixer/cpp/wire.h>
#include <lib/zx/profile.h>

#include <memory>
#include <string>

#include "src/media/audio/mixer_service/common/basic_types.h"
#include "src/media/audio/mixer_service/common/global_task_queue.h"
#include "src/media/audio/mixer_service/common/timer.h"
#include "src/media/audio/mixer_service/mix/thread.h"

namespace media_audio {

// A mix thread encapsulates a kernel thread and all work performed on that thread,
// which includes mix jobs and other operations that must execute on a mix thread.
// See discussion in ../README.md.
//
// This class is not thread safe: with the exception of a few const methods, all methods
// on this class must be called from the kernel thread owned by this thread. This is
// usually done by posting a closure to the GlobalTaskQueue.
class MixThread : public Thread {
 public:
  // Caller must ensure that `id` is a unique identifier for this thread.
  // The thread takes ownership of all handles in `options`.
  static MixThreadPtr Create(ThreadId id,
                             fuchsia_audio_mixer::wire::GraphCreateThreadRequest& options,
                             std::shared_ptr<GlobalTaskQueue> global_task_queue,
                             std::shared_ptr<Timer> timer);

  // Returns the thread's ID.
  // This is guaranteed to be a unique identifier.
  // Safe to call from any thread.
  ThreadId id() const override { return id_; }

  // Returns the thread's name. This is used for diagnostics only.
  // The name may not be a unique identifier.
  // Safe to call from any thread.
  std::string_view name() const override { return name_; }

  // Returns a checker which validates that code is running on this thread.
  // Safe to call from any thread.
  const ThreadChecker& checker() const override { return *checker_; }

  // Add a consumer to this thread.
  // This thread becomes responsible for running mix jobs on this consumer.
  void AddConsumer(ConsumerStagePtr consumer) override TA_REQ(checker());

  // Remove a consumer from this thread.
  void RemoveConsumer(ConsumerStagePtr consumer) override TA_REQ(checker());

  // Shuts down this thread.
  // The underlying kernel thread will tear itself down asynchronously.
  void Shutdown() TA_REQ(checker());

 private:
  MixThread(ThreadId id, fuchsia_audio_mixer::wire::GraphCreateThreadRequest& options,
            std::shared_ptr<GlobalTaskQueue> global_task_queue, std::shared_ptr<Timer> timer);

  static void Run(MixThreadPtr thread);
  void RunLoop();

  const ThreadId id_;
  const std::string name_;
  const zx::profile deadline_profile_;
  const std::shared_ptr<GlobalTaskQueue> global_task_queue_;
  const std::shared_ptr<Timer> timer_;

  // Logically const, but cannot be created until after we've created the std::thread,
  // which we can't do until after the ctor. See implementation of MixThread::Create.
  std::unique_ptr<ThreadChecker> checker_;

  // Used to synchronize MixThread::Create and MixThread::Run.
  std::mutex startup_mutex_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_MIX_THREAD_H_
