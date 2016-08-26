// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/services/common/timeline_control_point.h"

#include "apps/media/cpp/timeline.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace mojo {
namespace media {

// For checking preconditions when handling mojo requests.
// Checks the condition, and, if it's false, resets and calls return.
#define RCHECK(condition)                                         \
  if (!(condition)) {                                             \
    LOG(ERROR) << "request precondition failed: " #condition "."; \
    ResetUnsafe();                                                \
    return;                                                       \
  }

TimelineControlPoint::TimelineControlPoint()
    : control_point_binding_(this), consumer_binding_(this) {
  task_runner_ = base::MessageLoop::current()->task_runner();
  DCHECK(task_runner_);

  ftl::MutexLocker locker(&mutex_);
  ClearPendingTimelineFunctionUnsafe(false);

  status_publisher_.SetCallbackRunner(
      [this](const GetStatusCallback& callback, uint64_t version) {
        MediaTimelineControlPointStatusPtr status;
        {
          ftl::MutexLocker locker(&mutex_);
          status = MediaTimelineControlPointStatus::New();
          status->timeline_transform =
              TimelineTransform::From(current_timeline_function_);
          status->end_of_stream = ReachedEndOfStreamUnsafe();
        }
        callback.Run(version, status.Pass());
      });
}

TimelineControlPoint::~TimelineControlPoint() {}

void TimelineControlPoint::Bind(
    InterfaceRequest<MediaTimelineControlPoint> request) {
  if (control_point_binding_.is_bound()) {
    control_point_binding_.Close();
  }

  control_point_binding_.Bind(request.Pass());
}

void TimelineControlPoint::Reset() {
  if (control_point_binding_.is_bound()) {
    control_point_binding_.Close();
  }

  if (consumer_binding_.is_bound()) {
    consumer_binding_.Close();
  }

  {
    ftl::MutexLocker locker(&mutex_);
    current_timeline_function_ = TimelineFunction();
    ClearPendingTimelineFunctionUnsafe(false);
    generation_ = 1;
  }

  status_publisher_.SendUpdates();
}

void TimelineControlPoint::SnapshotCurrentFunction(int64_t reference_time,
                                                   TimelineFunction* out,
                                                   uint32_t* generation) {
  DCHECK(out);
  ftl::MutexLocker locker(&mutex_);
  ApplyPendingChangesUnsafe(reference_time);
  *out = current_timeline_function_;
  if (generation) {
    *generation = generation_;
  }

  if (ReachedEndOfStreamUnsafe() && !end_of_stream_published_) {
    end_of_stream_published_ = true;
    task_runner_->PostTask(
        FROM_HERE, base::Bind(&MojoPublisher<GetStatusCallback>::SendUpdates,
                              base::Unretained(&status_publisher_)));
  }
}

void TimelineControlPoint::SetEndOfStreamPts(int64_t end_of_stream_pts) {
  ftl::MutexLocker locker(&mutex_);
  if (end_of_stream_pts_ != end_of_stream_pts) {
    end_of_stream_pts_ = end_of_stream_pts;
    end_of_stream_published_ = false;
  }
}

bool TimelineControlPoint::ReachedEndOfStreamUnsafe() {
  mutex_.AssertHeld();

  return end_of_stream_pts_ != kUnspecifiedTime &&
         current_timeline_function_(Timeline::local_now()) >=
             end_of_stream_pts_;
}

void TimelineControlPoint::GetStatus(uint64_t version_last_seen,
                                     const GetStatusCallback& callback) {
  status_publisher_.Get(version_last_seen, callback);
}

void TimelineControlPoint::GetTimelineConsumer(
    InterfaceRequest<TimelineConsumer> timeline_consumer) {
  if (consumer_binding_.is_bound()) {
    consumer_binding_.Close();
  }

  consumer_binding_.Bind(timeline_consumer.Pass());
}

void TimelineControlPoint::Prime(const PrimeCallback& callback) {
  if (prime_requested_callback_) {
    prime_requested_callback_(callback);
  } else {
    callback.Run();
  }
}

void TimelineControlPoint::SetTimelineTransform(
    TimelineTransformPtr timeline_transform,
    const SetTimelineTransformCallback& callback) {
  ftl::MutexLocker locker(&mutex_);

  RCHECK(timeline_transform);
  RCHECK(timeline_transform->reference_delta != 0);

  if (timeline_transform->subject_time != kUnspecifiedTime &&
      end_of_stream_pts_ != kUnspecifiedTime) {
    end_of_stream_pts_ = kUnspecifiedTime;
    end_of_stream_published_ = false;
  }

  int64_t reference_time =
      timeline_transform->reference_time == kUnspecifiedTime
          ? Timeline::local_now()
          : timeline_transform->reference_time;
  int64_t subject_time = timeline_transform->subject_time == kUnspecifiedTime
                             ? current_timeline_function_(reference_time)
                             : timeline_transform->subject_time;

  // Eject any previous pending change.
  ClearPendingTimelineFunctionUnsafe(false);

  // Queue up the new pending change.
  pending_timeline_function_ = TimelineFunction(
      reference_time, subject_time, timeline_transform->reference_delta,
      timeline_transform->subject_delta);

  set_timeline_transform_callback_ = callback;
}

void TimelineControlPoint::ApplyPendingChangesUnsafe(int64_t reference_time) {
  mutex_.AssertHeld();

  if (!TimelineFunctionPendingUnsafe() ||
      pending_timeline_function_.reference_time() > reference_time) {
    return;
  }

  current_timeline_function_ = pending_timeline_function_;
  ClearPendingTimelineFunctionUnsafe(true);

  ++generation_;

  task_runner_->PostTask(
      FROM_HERE, base::Bind(&MojoPublisher<GetStatusCallback>::SendUpdates,
                            base::Unretained(&status_publisher_)));
}

void TimelineControlPoint::ClearPendingTimelineFunctionUnsafe(bool completed) {
  mutex_.AssertHeld();

  pending_timeline_function_ =
      TimelineFunction(kUnspecifiedTime, kUnspecifiedTime, 1, 0);
  if (!set_timeline_transform_callback_.is_null()) {
    task_runner_->PostTask(
        FROM_HERE, base::Bind(&TimelineControlPoint::RunCallback,
                              set_timeline_transform_callback_, completed));
    set_timeline_transform_callback_.reset();
  }
}

void TimelineControlPoint::ResetUnsafe() {
  mutex_.AssertHeld();
  task_runner_->PostTask(FROM_HERE, base::Bind(&TimelineControlPoint::Reset,
                                               base::Unretained(this)));
}

// static
void TimelineControlPoint::RunCallback(SetTimelineTransformCallback callback,
                                       bool completed) {
  callback.Run(completed);
}

}  // namespace media
}  // namespace mojo
