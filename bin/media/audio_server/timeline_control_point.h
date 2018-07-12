// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_SERVER_TIMELINE_CONTROL_POINT_H_
#define GARNET_BIN_MEDIA_AUDIO_SERVER_TIMELINE_CONTROL_POINT_H_

#include <mutex>

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fit/function.h>

#include "garnet/bin/media/util/fidl_publisher.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/synchronization/thread_annotations.h"
#include "lib/media/timeline/timeline_function.h"

namespace media {

// MediaTimelineControlPoint implementation.
class TimelineControlPoint : public fuchsia::media::MediaTimelineControlPoint,
                             public fuchsia::media::TimelineConsumer {
 public:
  using ProgramRangeSetCallback =
      fit::function<void(uint64_t, int64_t, int64_t)>;
  using PrimeRequestedCallback = fit::function<void(PrimeCallback)>;
  using ProgressStartedCallback = fit::closure;

  TimelineControlPoint();

  ~TimelineControlPoint() override;

  // Binds to the control point. If a binding exists already, it is closed.
  void Bind(fidl::InterfaceRequest<fuchsia::media::MediaTimelineControlPoint>
                request);

  // Determines whether the control point is currently bound.
  bool is_bound() { return control_point_binding_.is_bound(); }

  // Unbinds from clients and resets to initial state.
  void Reset();

  // Sets a callback to be called when priming is requested.
  void SetProgramRangeSetCallback(ProgramRangeSetCallback callback) {
    program_range_set_callback_ = std::move(callback);
  }

  // Sets a callback to be called when priming is requested.
  void SetPrimeRequestedCallback(PrimeRequestedCallback callback) {
    prime_requested_callback_ = std::move(callback);
  }

  // Sets a callback to be called when priming is requested.
  void SetProgressStartedCallback(ProgressStartedCallback callback) {
    progress_started_callback_ = std::move(callback);
  }

  // Determines if presentation time is progressing or a pending change will
  // cause it to progress.
  bool Progressing() {
    std::lock_guard<std::mutex> locker(mutex_);
    return ProgressingInternal();
  }

  // Get the TimelineFunction for the reference_time (which should be 'now',
  // approximately).
  void SnapshotCurrentFunction(int64_t reference_time, TimelineFunction* out,
                               uint32_t* generation = nullptr);

  // Sets the current end_of_stream status published by the control point.
  void SetEndOfStreamPts(int64_t end_of_stream_pts);

  // Clears a pending end-of-stream transition scheduled with
  // |SetEndOfStreamPts|.
  void ClearEndOfStream();

  // MediaTimelineControlPoint implementation.
  void GetStatus(uint64_t version_last_seen,
                 GetStatusCallback callback) override;

  void GetTimelineConsumer(
      fidl::InterfaceRequest<fuchsia::media::TimelineConsumer>
          timeline_consumer) override;

  void SetProgramRange(uint64_t program, int64_t min_pts,
                       int64_t max_pts) override;

  void Prime(PrimeCallback callback) override;

  // TimelineConsumer implementation.
  void SetTimelineTransform(
      fuchsia::media::TimelineTransform timeline_transform,
      SetTimelineTransformCallback callback) override;

  void SetTimelineTransformNoReply(
      fuchsia::media::TimelineTransform timeline_transform) override;

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
    return pending_timeline_function_.reference_time() !=
           fuchsia::media::kUnspecifiedTime;
  }

  // Determines whether end-of-stream has been reached.
  bool ReachedEndOfStream() FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Unbinds from clients and resets to initial state.
  void PostReset() FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Determines if presentation time is progressing or a pending change will
  // cause it to progress.
  bool ProgressingInternal() FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  void SetTimelineTransformLocked(
      fuchsia::media::TimelineTransform timeline_transform)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  fidl::Binding<fuchsia::media::MediaTimelineControlPoint>
      control_point_binding_;
  fidl::Binding<fuchsia::media::TimelineConsumer> consumer_binding_;
  media::FidlPublisher<GetStatusCallback> status_publisher_;
  ProgramRangeSetCallback program_range_set_callback_;
  PrimeRequestedCallback prime_requested_callback_;
  ProgressStartedCallback progress_started_callback_;

  std::mutex mutex_;
  async_dispatcher_t* dispatcher_ FXL_GUARDED_BY(mutex_);
  TimelineFunction current_timeline_function_ FXL_GUARDED_BY(mutex_);
  TimelineFunction pending_timeline_function_ FXL_GUARDED_BY(mutex_);
  SetTimelineTransformCallback set_timeline_transform_callback_
      FXL_GUARDED_BY(mutex_);
  uint32_t generation_ FXL_GUARDED_BY(mutex_) = 1;
  int64_t end_of_stream_pts_ FXL_GUARDED_BY(mutex_) =
      fuchsia::media::kUnspecifiedTime;
  bool end_of_stream_published_ FXL_GUARDED_BY(mutex_) = false;
};

}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_SERVER_TIMELINE_CONTROL_POINT_H_
