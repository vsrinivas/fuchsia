// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/media/util/fidl_publisher.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/synchronization/mutex.h"
#include "lib/fxl/synchronization/thread_annotations.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/media/fidl/logs/media_timeline_control_point_channel.fidl.h"
#include "lib/media/fidl/timeline_controller.fidl.h"
#include "lib/media/flog/flog.h"
#include "lib/media/timeline/timeline_function.h"

namespace media {

// MediaTimelineControlPoint implementation.
class TimelineControlPoint : public MediaTimelineControlPoint,
                             public TimelineConsumer {
 public:
  using ProgramRangeSetCallback =
      std::function<void(uint64_t, int64_t, int64_t)>;
  using PrimeRequestedCallback = std::function<void(const PrimeCallback&)>;
  using ProgressStartedCallback = std::function<void()>;

  TimelineControlPoint();

  ~TimelineControlPoint() override;

  // Binds to the control point. If a binding exists already, it is closed.
  void Bind(fidl::InterfaceRequest<MediaTimelineControlPoint> request);

  // Determines whether the control point is currently bound.
  bool is_bound() { return control_point_binding_.is_bound(); }

  // Unbinds from clients and resets to initial state.
  void Reset();

  // Sets a callback to be called when priming is requested.
  void SetProgramRangeSetCallback(const ProgramRangeSetCallback& callback) {
    program_range_set_callback_ = callback;
  }

  // Sets a callback to be called when priming is requested.
  void SetPrimeRequestedCallback(const PrimeRequestedCallback& callback) {
    prime_requested_callback_ = callback;
  }

  // Sets a callback to be called when priming is requested.
  void SetProgressStartedCallback(const ProgressStartedCallback& callback) {
    progress_started_callback_ = callback;
  }

  // Determines if presentation time is progressing or a pending change will
  // cause it to progress.
  bool Progressing() {
    fxl::MutexLocker locker(&mutex_);
    return ProgressingInternal();
  }

  // Get the TimelineFunction for the reference_time (which should be 'now',
  // approximately).
  void SnapshotCurrentFunction(int64_t reference_time,
                               TimelineFunction* out,
                               uint32_t* generation = nullptr);

  // Sets the current end_of_stream status published by the control point.
  void SetEndOfStreamPts(int64_t end_of_stream_pts);

  // Clears a pending end-of-stream transition scheduled with
  // |SetEndOfStreamPts|.
  void ClearEndOfStream();

  // MediaTimelineControlPoint implementation.
  void GetStatus(uint64_t version_last_seen,
                 const GetStatusCallback& callback) override;

  void GetTimelineConsumer(
      fidl::InterfaceRequest<TimelineConsumer> timeline_consumer) override;

  void SetProgramRange(uint64_t program,
                       int64_t min_pts,
                       int64_t max_pts) override;

  void Prime(const PrimeCallback& callback) override;

  // TimelineConsumer implementation.
  void SetTimelineTransform(
      TimelineTransformPtr timeline_transform,
      const SetTimelineTransformCallback& callback) override;

  void SetTimelineTransformNoReply(
      TimelineTransformPtr timeline_transform) override;

 private:
  // Applies pending_timeline_function_ if it's time to do so based on the
  // given reference time.
  void ApplyPendingChanges(int64_t reference_time)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Clears the pending timeline function and calls its associated callback
  // with the indicated completed status.
  void ClearPendingTimelineFunction(bool completed)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Determines if an unrealized timeline function is currently pending.
  bool TimelineFunctionPending() FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    return pending_timeline_function_.reference_time() != kUnspecifiedTime;
  }

  // Determines whether end-of-stream has been reached.
  bool ReachedEndOfStream() FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Unbinds from clients and resets to initial state.
  void PostReset() FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Determines if presentation time is progressing or a pending change will
  // cause it to progress.
  bool ProgressingInternal() FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  void SetTimelineTransformLocked(TimelineTransformPtr timeline_transform)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  fidl::Binding<MediaTimelineControlPoint> control_point_binding_;
  fidl::Binding<TimelineConsumer> consumer_binding_;
  FidlPublisher<GetStatusCallback> status_publisher_;
  ProgramRangeSetCallback program_range_set_callback_;
  PrimeRequestedCallback prime_requested_callback_;
  ProgressStartedCallback progress_started_callback_;

  fxl::Mutex mutex_;
  fxl::RefPtr<fxl::TaskRunner> task_runner_ FXL_GUARDED_BY(mutex_);
  TimelineFunction current_timeline_function_ FXL_GUARDED_BY(mutex_);
  TimelineFunction pending_timeline_function_ FXL_GUARDED_BY(mutex_);
  SetTimelineTransformCallback set_timeline_transform_callback_
      FXL_GUARDED_BY(mutex_);
  uint32_t generation_ FXL_GUARDED_BY(mutex_) = 1;
  int64_t end_of_stream_pts_ FXL_GUARDED_BY(mutex_) = kUnspecifiedTime;
  bool end_of_stream_published_ FXL_GUARDED_BY(mutex_) = false;

  FLOG_INSTANCE_CHANNEL(logs::MediaTimelineControlPointChannel, log_channel_);
};

}  // namespace media
