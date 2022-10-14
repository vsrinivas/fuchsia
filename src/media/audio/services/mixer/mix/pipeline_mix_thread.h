// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_PIPELINE_MIX_THREAD_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_PIPELINE_MIX_THREAD_H_

#include <fidl/fuchsia.audio.mixer/cpp/wire.h>
#include <lib/sync/cpp/completion.h>
#include <lib/zx/profile.h>
#include <lib/zx/time.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/media/audio/lib/clock/clock.h"
#include "src/media/audio/lib/clock/clock_snapshot.h"
#include "src/media/audio/lib/clock/timer.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/common/global_task_queue.h"
#include "src/media/audio/services/mixer/mix/pipeline_thread.h"

namespace media_audio {

// A mix thread encapsulates a kernel thread and all work performed on that thread, which includes
// mix jobs and other operations that must execute on a mix thread. This class is essentially just a
// set of ConsumerStages, plus a thread that does:
//
// ```
// for (;;) {
//   SleepUntil(next_period);
//   for (auto c : consumers) {
//     c->RunMixJob(...);
//   }
// }
// ```
//
// See discussion in ../README.md.
//
// This class is not thread safe: with the exception of a few const methods, all methods
// on this class must be called from the kernel thread owned by this thread. This is
// usually done by posting a closure to the GlobalTaskQueue.
class PipelineMixThread : public PipelineThread {
 public:
  struct Args {
    // Caller must ensure that `id` is a unique identifier for this thread.
    ThreadId id;

    // Name for this thread. This is used for diagnostics only.
    // The name may not be a unique identifier.
    std::string_view name;

    // Deadline profile to apply to the kernel thread backing this PipelineMixThread.
    // Optional: this may be an invalid handle if a deadline profile should not be applied.
    zx::profile deadline_profile;

    // This thread will process audio in batches of size `mix_period`.
    // Must be positive.
    zx::duration mix_period;

    // Each mix period should take less than `cpu_per_period` of CPU time.
    // Must be positive and not greater than `mix_period`.
    zx::duration cpu_per_period;

    // This thread will be responsible for running all tasks with a matching thread `id`.
    std::shared_ptr<GlobalTaskQueue> global_task_queue;

    // Timer to use when going to sleep.
    std::shared_ptr<Timer> timer;

    // Handle to the system monotonic clock.
    std::shared_ptr<const Clock> mono_clock;
  };

  static std::shared_ptr<PipelineMixThread> Create(Args args);

  // Implementation of Thread.
  ThreadId id() const override { return id_; }
  std::string_view name() const override { return name_; }
  const ThreadChecker& checker() const override { return *checker_; }

  // Reports the mix period.
  zx::duration mix_period() const { return mix_period_; }

  // Shuts down this thread.
  // The underlying kernel thread will tear itself down asynchronously.
  void Shutdown() TA_REQ(checker());

  // Adds a consumer to this thread.
  // This thread becomes responsible for running mix jobs on this consumer.
  void AddConsumer(ConsumerStagePtr consumer) TA_REQ(checker());

  // Removes a consumer from this thread.
  void RemoveConsumer(ConsumerStagePtr consumer) TA_REQ(checker());

  // Notifies this thread that `consumer` is about to start running. This should be called
  // immediately after a StartCommand is sent to `consumer`, and also after AddConsumer if the
  // consumer may have been previously started.
  void NotifyConsumerStarting(ConsumerStagePtr consumer) TA_REQ(checker());

  // Adds/removes a clock. A clock should be added when it is used by any mix job controlled by this
  // thread, and removed when it's no longer needed by any mix jobs.
  void AddClock(std::shared_ptr<const Clock> clock) TA_REQ(checker()) {
    clocks_.AddClock(std::move(clock));
  }
  void RemoveClock(std::shared_ptr<const Clock> clock) TA_REQ(checker()) {
    clocks_.RemoveClock(std::move(clock));
  }

 private:
  // For RunMixJobs.
  friend class PipelineMixThreadRunMixJobsTest;

  // For testing only: like Create, but reuses the current thread and doesn't start a RunLoop.
  friend std::shared_ptr<PipelineMixThread> CreatePipelineMixThreadWithoutLoop(Args args);
  static std::shared_ptr<PipelineMixThread> CreateWithoutLoop(Args args);

  explicit PipelineMixThread(Args args);

  static void Run(std::shared_ptr<PipelineMixThread> thread,
                  std::shared_ptr<libsync::Completion> checker_ready,
                  std::shared_ptr<libsync::Completion> task_queue_ready);
  void RunLoop() TA_REQ(checker());

  // Run mix jobs for all consumers. The mix jobs are scheduled to run during the period
  // `[mono_start_time, mono_start_time + period_]`. The current time, `mono_now`, should be within
  // that period. If `mono_now` is after that period, the jobs have underflowed. Returns the start
  // time of the next job, or `zx::time::infinite()` if there is no next job (i.e., the thread is
  // idle).
  zx::time RunMixJobs(zx::time mono_start_time, zx::time mono_now) TA_REQ(checker());

  const ThreadId id_;
  const std::string name_;
  const zx::profile deadline_profile_;
  const zx::duration mix_period_;
  const zx::duration cpu_per_period_;
  const std::shared_ptr<GlobalTaskQueue> global_task_queue_;
  const std::shared_ptr<Timer> timer_;
  const std::shared_ptr<const Clock> mono_clock_;

  // Logically const, but cannot be created until after we've created the std::thread,
  // which we can't do until after the ctor. See implementation of PipelineMixThread::Create.
  std::unique_ptr<ThreadChecker> checker_;

  // Set of clocks used by this thread.
  TA_GUARDED(checker()) ClockSnapshots clocks_;

  // All consumers attached to this thread.
  struct ConsumerInfo {
    bool maybe_started;                               // true if the consumer might be running
    std::optional<zx::time> next_mix_job_start_time;  // if stopped, the next start time
  };
  TA_GUARDED(checker()) std::unordered_map<ConsumerStagePtr, ConsumerInfo> consumers_;

  // Current loop state.
  enum State {
    Idle,
    WakeFromIdle,
    Running,
  };
  TA_GUARDED(checker()) State state_{State::Idle};
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_PIPELINE_MIX_THREAD_H_
