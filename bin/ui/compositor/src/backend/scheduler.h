// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_GFX_COMPOSITOR_BACKEND_SCHEDULER_H_
#define SERVICES_GFX_COMPOSITOR_BACKEND_SCHEDULER_H_

#include <functional>
#include <limits>
#include <mutex>

#include "apps/compositor/services/interfaces/scheduling.mojom.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"

namespace compositor {

// A frame scheduler is responsible for deciding when to perform each
// phase of composition.
//
// During the "update" phase, the compositor signals each application that
// that it should start producing the next frame of content.
//
// During the "snapshot" phase, the compositor gathers all pending scene
// graph updates and produces a new frame for rendering.  Rendering begins
// immediately after the snapshot is taken.
//
// An instance of the |Scheduler| interface is exposed by each |Output|
// so as to express the timing requirements of the output.
class Scheduler : public ftl::RefCountedThreadSafe<Scheduler> {
 public:
  // Determines the behavior of |ScheduleFrame()|.
  enum class SchedulingMode {
    // Schedules a snapshot, at minimum.
    kSnapshot,

    // Schedules an update followed by a snapshot, at minimum.
    kUpdateAndSnapshot,
  };

  Scheduler() = default;

  // Schedules work for a frame.
  //
  // This function ensures that every update is followed by a snapshot
  // unless scheduling is suspended in the meantime.
  //
  // When |scheduling_mode| is |kSnapshot|, if there is time between now
  // and the snapshot during which an update can be performed, then an
  // update will also be scheduled before the requested snapshot.
  //
  // When |scheduling_mode| is |kUpdateAndSnapshot|, if there is time
  // between now and the update during which a snapshot can be performed,
  // then a snapshot will also be scheduled before the requested update
  // and the next snapshot.
  //
  // This design is intended to minimize latency by anticipating that
  // snapshots will be needed after updates and by scheduling updates in
  // advance if it is known that a snapshot will be needed on the next frame.
  virtual void ScheduleFrame(SchedulingMode scheduling_mode) = 0;

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(Scheduler);
  virtual ~Scheduler() = default;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(Scheduler);
};

// Scheduling callbacks.
//
// These callbacks are provided to the |Output| in order to receive the
// events produced by the output's associated |Scheduler|.
struct SchedulerCallbacks {
  using FrameCallback =
      std::function<void(const mojo::gfx::composition::FrameInfo&)>;

  SchedulerCallbacks(const FrameCallback& update_callback,
                     const FrameCallback& snapshot_callback);
  ~SchedulerCallbacks();

  // Called when it's time for applications to/ update the contents of
  // their scenes.
  const FrameCallback update_callback;

  // Called when it's time for the compositor to snapshot and submit
  // the next frame.
  const FrameCallback snapshot_callback;
};

}  // namespace compositor

#endif  // SERVICES_GFX_COMPOSITOR_BACKEND_SCHEDULER_H_
