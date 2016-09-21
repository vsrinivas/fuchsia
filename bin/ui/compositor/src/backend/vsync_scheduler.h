// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_GFX_COMPOSITOR_BACKEND_VSYNC_SCHEDULER_H_
#define SERVICES_GFX_COMPOSITOR_BACKEND_VSYNC_SCHEDULER_H_

#include <functional>
#include <limits>
#include <memory>
#include <mutex>

#include "apps/compositor/src/backend/scheduler.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"
#include "lib/ftl/tasks/task_runner.h"

namespace compositor {

// Schedules work to coincide with vsync intervals.
//
// This object is thread-safe and it intended to be used to allow one thread
// to be scheduling work for itself while another thread concurrently updates
// timing parameters.
class VsyncScheduler : public Scheduler {
 public:
  // Limits on allowable parameters.  (Exposed for testing.)
  static constexpr int64_t kMinVsyncInterval = 1000;     // 1000 Hz
  static constexpr int64_t kMaxVsyncInterval = 1000000;  // 1 Hz

  // Time reference.  Should be MojoTimeTicksNow() except during testing.
  using Clock = std::function<MojoTimeTicks()>;

  VsyncScheduler(const ftl::RefPtr<ftl::TaskRunner>& task_runner,
                 const SchedulerCallbacks& callbacks);
  VsyncScheduler(const ftl::RefPtr<ftl::TaskRunner>& task_runner,
                 const SchedulerCallbacks& callbacks,
                 const Clock& clock);

  // Starts scheduling work and sets the scheduling parameters.
  //
  // |vsync_timebase| is a value in the |MojoTimeTicks| timebase which
  // specifies when a recent vsync occurred and is used to determine the phase.
  //
  // |vsync_interval| is the number of microseconds between vsyncs which
  // also determines the |FrameInfo.frame_interval| value to deliver to
  // applications.
  //
  // |update_phase| specifies an offset relative to vsync for determining
  // when updates are scheduled and the |FrameInfo.frame_time| to deliver
  // to applications.
  //
  // |snapshot_phase| specifies an offset relative to vsync for
  // determining when snapshots are scheduled and the |FrameInfo.frame_deadline|
  // to deliver to applications.  Must be greater than or equal to
  // |update_phase|.
  //
  // |presentation_phase| specifies an offset relative to vsync for
  // determining when frames are shown on the display output and the
  // |FrameInfo.presentation_time| to deliver to applications.  Must be
  // greater than or equal to |snapshot_phase|.
  //
  // The notion of 'vsync' is somewhat abstract here.  It's just a reference
  // pulse but we usually interpret it as a deadline for preparing the next
  // frame and submitting it to the display hardware.
  //
  // The phases can be positive or negative but negative offsets from vsync
  // may be easier to interpret when computing deadlines.  To avoid
  // overflows, the values chosen for the phases should be close to 0.
  //
  // This function schedules an update and snapshot if not already scheduled.
  //
  // Returns true if the schedule was started successfully, false if the
  // parameters are invalid.
  bool Start(int64_t vsync_timebase,
             int64_t vsync_interval,
             int64_t update_phase,
             int64_t snapshot_phase,
             int64_t presentation_phase) {
    return state_->Start(vsync_timebase, vsync_interval, update_phase,
                         snapshot_phase, presentation_phase);
  }

  // Stops scheduling work.
  //
  // Previously scheduled callbacks may still be delivered.
  void Stop() { state_->Stop(); }

  // |Scheduler|:
  void ScheduleFrame(SchedulingMode scheduling_mode) override;

 protected:
  ~VsyncScheduler() override;

 private:
  // Internal state.  Held by a shared_ptr so that callbacks running on
  // other threads can reference it using a weak_ptr.
  class State : public std::enable_shared_from_this<State> {
   public:
    State(const ftl::RefPtr<ftl::TaskRunner>& task_runner,
          const SchedulerCallbacks& callbacks,
          const Clock& clock);
    ~State();

    MojoTimeTicks GetTimeTicksNow() { return clock_(); }

    bool Start(int64_t vsync_timebase,
               int64_t vsync_interval,
               int64_t update_phase,
               int64_t snapshot_phase,
               int64_t presentation_phase);
    void Stop();
    void ScheduleFrame(SchedulingMode scheduling_mode);

   private:
    enum class Action {
      kUpdate,
      kEarlySnapshot,
      kLateSnapshot,
    };

    void ScheduleLocked(MojoTimeTicks now);
    void PostDispatchLocked(int64_t now,
                            int64_t delivery_time,
                            Action action,
                            int64_t update_time);

    void Dispatch(int32_t generation, Action action, int64_t update_time);
    void SetFrameInfoLocked(mojo::gfx::composition::FrameInfo* frame_info,
                            int64_t update_time);

    const ftl::RefPtr<ftl::TaskRunner> task_runner_;
    const SchedulerCallbacks callbacks_;
    const Clock clock_;

    // Parameters and state guarded by |mutex_|.
    std::mutex mutex_;
    bool running_ = false;
    int32_t generation_ = 0;
    int64_t vsync_timebase_ = 0;
    int64_t vsync_interval_ = 0;
    int64_t update_phase_ = 0;
    int64_t snapshot_phase_ = 0;
    int64_t presentation_phase_ = 0;
    bool need_update_ = false;
    bool pending_dispatch_ = false;
    int64_t last_delivered_update_time_ = std::numeric_limits<int64_t>::min();
    int64_t last_delivered_presentation_time_ =
        std::numeric_limits<int64_t>::min();

    FTL_DISALLOW_COPY_AND_ASSIGN(State);
  };

  const std::shared_ptr<State> state_;

  FTL_DISALLOW_COPY_AND_ASSIGN(VsyncScheduler);
};

}  // namespace compositor

#endif  // SERVICES_GFX_COMPOSITOR_BACKEND_VSYNC_SCHEDULER_H_
