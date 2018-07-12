// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/timeline_control_point.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline.h"
#include "lib/media/timeline/timeline_function.h"

namespace media {

// For checking preconditions when handling fidl requests.
// Checks the condition, and, if it's false, resets and calls return.
#define RCHECK(condition)                                             \
  if (!(condition)) {                                                 \
    FXL_LOG(ERROR) << "request precondition failed: " #condition "."; \
    PostReset();                                                      \
    return;                                                           \
  }

TimelineControlPoint::TimelineControlPoint()
    : control_point_binding_(this),
      consumer_binding_(this),
      dispatcher_(async_get_default_dispatcher()) {
  FXL_DCHECK(dispatcher_);

  std::lock_guard<std::mutex> locker(mutex_);
  ClearPendingTimelineFunction(false);

  status_publisher_.SetCallbackRunner(
      [this](GetStatusCallback callback, uint64_t version) {
        fuchsia::media::MediaTimelineControlPointStatus status;
        {
          std::lock_guard<std::mutex> locker(mutex_);
          status.timeline_transform =
              current_timeline_function_.ToTimelineTransform();
          status.end_of_stream = ReachedEndOfStream();
        }
        callback(version, std::move(status));
      });
}

TimelineControlPoint::~TimelineControlPoint() {
  // Close the bindings before members are destroyed so we don't try to
  // destroy any callbacks that are pending on open channels.

  if (control_point_binding_.is_bound()) {
    control_point_binding_.Unbind();
  }

  if (consumer_binding_.is_bound()) {
    consumer_binding_.Unbind();
  }
}

void TimelineControlPoint::Bind(
    fidl::InterfaceRequest<fuchsia::media::MediaTimelineControlPoint> request) {
  if (control_point_binding_.is_bound()) {
    control_point_binding_.Unbind();
  }

  control_point_binding_.Bind(std::move(request));
}

void TimelineControlPoint::Reset() {
  if (control_point_binding_.is_bound()) {
    control_point_binding_.Unbind();
  }

  if (consumer_binding_.is_bound()) {
    consumer_binding_.Unbind();
  }

  {
    std::lock_guard<std::mutex> locker(mutex_);
    current_timeline_function_ = TimelineFunction();
    ClearPendingTimelineFunction(false);
    generation_ = 1;
  }

  status_publisher_.SendUpdates();
}

void TimelineControlPoint::SnapshotCurrentFunction(int64_t reference_time,
                                                   TimelineFunction* out,
                                                   uint32_t* generation) {
  FXL_DCHECK(out);
  std::lock_guard<std::mutex> locker(mutex_);
  ApplyPendingChanges(reference_time);
  *out = current_timeline_function_;
  if (generation) {
    *generation = generation_;
  }

  if (ReachedEndOfStream() && !end_of_stream_published_) {
    end_of_stream_published_ = true;
    async::PostTask(dispatcher_, [this]() { status_publisher_.SendUpdates(); });
  }
}

void TimelineControlPoint::SetEndOfStreamPts(int64_t end_of_stream_pts) {
  std::lock_guard<std::mutex> locker(mutex_);
  if (end_of_stream_pts_ != end_of_stream_pts) {
    end_of_stream_pts_ = end_of_stream_pts;
    end_of_stream_published_ = false;
  }
}

void TimelineControlPoint::ClearEndOfStream() {
  std::lock_guard<std::mutex> locker(mutex_);
  if (end_of_stream_pts_ != fuchsia::media::kUnspecifiedTime) {
    end_of_stream_pts_ = fuchsia::media::kUnspecifiedTime;
    end_of_stream_published_ = false;
  }
}

bool TimelineControlPoint::ReachedEndOfStream() {
  return end_of_stream_pts_ != fuchsia::media::kUnspecifiedTime &&
         current_timeline_function_(Timeline::local_now()) >=
             end_of_stream_pts_;
}

void TimelineControlPoint::GetStatus(uint64_t version_last_seen,
                                     GetStatusCallback callback) {
  status_publisher_.Get(version_last_seen, std::move(callback));
}

void TimelineControlPoint::GetTimelineConsumer(
    fidl::InterfaceRequest<fuchsia::media::TimelineConsumer>
        timeline_consumer) {
  if (consumer_binding_.is_bound()) {
    consumer_binding_.Unbind();
  }

  consumer_binding_.Bind(std::move(timeline_consumer));
}

void TimelineControlPoint::SetProgramRange(uint64_t program, int64_t min_pts,
                                           int64_t max_pts) {
  if (program_range_set_callback_) {
    program_range_set_callback_(program, min_pts, max_pts);
  }
}

void TimelineControlPoint::Prime(PrimeCallback callback) {
  if (prime_requested_callback_) {
    prime_requested_callback_(
        [this, callback = std::move(callback)]() { callback(); });
  } else {
    callback();
  }
}

void TimelineControlPoint::SetTimelineTransform(
    fuchsia::media::TimelineTransform timeline_transform,
    SetTimelineTransformCallback callback) {
  std::lock_guard<std::mutex> locker(mutex_);

  SetTimelineTransformLocked(std::move(timeline_transform));

  set_timeline_transform_callback_ = std::move(callback);
}

void TimelineControlPoint::SetTimelineTransformNoReply(
    fuchsia::media::TimelineTransform timeline_transform) {
  std::lock_guard<std::mutex> locker(mutex_);

  SetTimelineTransformLocked(std::move(timeline_transform));
}

void TimelineControlPoint::SetTimelineTransformLocked(
    fuchsia::media::TimelineTransform timeline_transform) {
  RCHECK(timeline_transform.reference_delta != 0);

  bool was_progressing = ProgressingInternal();

  int64_t reference_time =
      timeline_transform.reference_time == fuchsia::media::kUnspecifiedTime
          ? Timeline::local_now()
          : timeline_transform.reference_time;
  int64_t subject_time =
      timeline_transform.subject_time == fuchsia::media::kUnspecifiedTime
          ? current_timeline_function_(reference_time)
          : timeline_transform.subject_time;

  // Eject any previous pending change.
  ClearPendingTimelineFunction(false);

  // Queue up the new pending change.
  pending_timeline_function_ = TimelineFunction(
      subject_time, reference_time, timeline_transform.subject_delta,
      timeline_transform.reference_delta);

  if (progress_started_callback_ && !was_progressing && ProgressingInternal()) {
    async::PostTask(dispatcher_, [this]() {
      if (progress_started_callback_) {
        progress_started_callback_();
      }
    });
  }
}

void TimelineControlPoint::ApplyPendingChanges(int64_t reference_time) {
  if (!TimelineFunctionPending() ||
      pending_timeline_function_.reference_time() > reference_time) {
    return;
  }

  current_timeline_function_ = pending_timeline_function_;
  ClearPendingTimelineFunction(true);

  ++generation_;

  async::PostTask(dispatcher_, [this]() { status_publisher_.SendUpdates(); });
}

void TimelineControlPoint::ClearPendingTimelineFunction(bool completed) {
  pending_timeline_function_ = TimelineFunction(
      fuchsia::media::kUnspecifiedTime, fuchsia::media::kUnspecifiedTime, 0, 1);
  if (set_timeline_transform_callback_) {
    SetTimelineTransformCallback callback =
        std::move(set_timeline_transform_callback_);
    async::PostTask(dispatcher_, [this, callback = std::move(callback),
                             completed]() { callback(completed); });
  }
}

void TimelineControlPoint::PostReset() {
  async::PostTask(dispatcher_, [this]() { Reset(); });
}

bool TimelineControlPoint::ProgressingInternal() {
  return !end_of_stream_published_ &&
         (current_timeline_function_.subject_delta() != 0 ||
          pending_timeline_function_.subject_delta() != 0);
}

}  // namespace media
