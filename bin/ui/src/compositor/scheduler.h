// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_COMPOSITOR_SCHEDULER_H_
#define APPS_MOZART_SRC_COMPOSITOR_SCHEDULER_H_

#include <functional>

#include "apps/mozart/services/composition/interfaces/scheduling.mojom.h"
#include "apps/mozart/src/compositor/backend/output.h"
#include "apps/mozart/src/compositor/frame_info.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "lib/ftl/tasks/task_runner.h"

namespace compositor {

// The scheduler is responsible for deciding when to perform each phase
// of composition for the scene graph associated with a particular renderer.
//
// During the "update" phase, the compositor signals each application that
// that it should start producing the next frame of content.
//
// During the "snapshot" phase, the compositor gathers all pending scene
// graph updates and produces a new frame for rendering.  Rendering begins
// immediately after the snapshot is taken.
class Scheduler {
 public:
  using FrameCallback = std::function<void(const FrameInfo&)>;

  // Determines the behavior of |ScheduleFrame()|.
  enum class SchedulingMode {
    // Schedules a snapshot.
    kSnapshot,

    // Schedules an update followed by a snapshot.
    kUpdateThenSnapshot,
  };

  // Creates a scheduler for a particular output.
  Scheduler(Output* output);

  ~Scheduler();

  // Sets the scheduler callbacks.
  // The |update_callback| is called when it's time for applications to update
  // the contents of their scenes.
  // The |snapshot_callback| is called when it's time for the compositor to
  // snapshot and submit the next frame.
  void SetCallbacks(FrameCallback update_callback,
                    FrameCallback snapshot_callback);

  // Schedules work for a frame.
  void ScheduleFrame(SchedulingMode scheduling_mode);

 private:
  void OnFrameScheduled(const Output::FrameTiming& timing);
  void PostUpdate(const FrameInfo& frame_info);
  void PostSnapshot(const FrameInfo& frame_info);
  void OnUpdate(const FrameInfo& frame_info);
  void OnSnapshot(const FrameInfo& frame_info);

  Output* output_;
  FrameCallback update_callback_;
  FrameCallback snapshot_callback_;
  ftl::RefPtr<ftl::TaskRunner> task_runner_;

  bool frame_scheduled_ = false;
  bool update_pending_ = false;

  ftl::TimePoint last_presentation_time_;
  ftl::TimePoint last_snapshot_time_;
  ftl::TimePoint last_update_time_;
  bool prevent_stall_ = false;

  ftl::WeakPtrFactory<Scheduler> weak_ptr_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Scheduler);
};

}  // namespace compositor

#endif  // APPS_MOZART_SRC_COMPOSITOR_SCHEDULER_H_
