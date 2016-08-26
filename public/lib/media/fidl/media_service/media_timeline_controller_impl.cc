// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/services/media_service/media_timeline_controller_impl.h"

#include "apps/media/cpp/timeline.h"
#include "apps/media/services/framework/util/callback_joiner.h"
#include "apps/media/services/framework_mojo/mojo_type_conversions.h"
#include "lib/ftl/logging.h"

namespace mojo {
namespace media {

// static
std::shared_ptr<MediaTimelineControllerImpl>
MediaTimelineControllerImpl::Create(
    InterfaceRequest<MediaTimelineController> request,
    MediaServiceImpl* owner) {
  return std::shared_ptr<MediaTimelineControllerImpl>(
      new MediaTimelineControllerImpl(request.Pass(), owner));
}

MediaTimelineControllerImpl::MediaTimelineControllerImpl(
    InterfaceRequest<MediaTimelineController> request,
    MediaServiceImpl* owner)
    : MediaServiceImpl::Product<MediaTimelineController>(this,
                                                         request.Pass(),
                                                         owner),
      control_point_binding_(this),
      consumer_binding_(this) {
  status_publisher_.SetCallbackRunner(
      [this](const GetStatusCallback& callback, uint64_t version) {
        MediaTimelineControlPointStatusPtr status =
            MediaTimelineControlPointStatus::New();
        status->timeline_transform =
            mojo::TimelineTransform::From(current_timeline_function_);
        status->end_of_stream = end_of_stream_;
        callback.Run(version, status.Pass());
      });
}

MediaTimelineControllerImpl::~MediaTimelineControllerImpl() {
  status_publisher_.SendUpdates();
}

void MediaTimelineControllerImpl::AddControlPoint(
    InterfaceHandle<MediaTimelineControlPoint> control_point) {
  control_point_states_.push_back(std::unique_ptr<ControlPointState>(
      new ControlPointState(this, MediaTimelineControlPointPtr::Create(
                                      std::move(control_point)))));

  control_point_states_.back()->HandleStatusUpdates();
}

void MediaTimelineControllerImpl::GetControlPoint(
    InterfaceRequest<MediaTimelineControlPoint> control_point) {
  if (control_point_binding_.is_bound()) {
    control_point_binding_.Close();
  }

  control_point_binding_.Bind(control_point.Pass());
}

void MediaTimelineControllerImpl::GetStatus(uint64_t version_last_seen,
                                            const GetStatusCallback& callback) {
  status_publisher_.Get(version_last_seen, callback);
}

void MediaTimelineControllerImpl::GetTimelineConsumer(
    InterfaceRequest<TimelineConsumer> timeline_consumer) {
  if (consumer_binding_.is_bound()) {
    consumer_binding_.Close();
  }

  consumer_binding_.Bind(timeline_consumer.Pass());
}

void MediaTimelineControllerImpl::Prime(const PrimeCallback& callback) {
  std::shared_ptr<CallbackJoiner> callback_joiner = CallbackJoiner::Create();

  for (const std::unique_ptr<ControlPointState>& control_point_state :
       control_point_states_) {
    control_point_state->control_point_->Prime(callback_joiner->NewCallback());
  }

  callback_joiner->WhenJoined(callback);
}

void MediaTimelineControllerImpl::SetTimelineTransform(
    TimelineTransformPtr timeline_transform,
    const SetTimelineTransformCallback& callback) {
  RCHECK(timeline_transform);
  RCHECK(timeline_transform->reference_delta != 0);

  // There can only be one SetTimelineTransform transition pending at any
  // moment, so a new SetTimelineTransform call that arrives before a previous
  // one completes cancels the previous one. This causes some problems for us,
  // because some control points may complete the previous transition while
  // others may not.
  //
  // We start by noticing that there's an incomplete previous transition, and
  // we 'cancel' it, meaning we call its callback with a false complete
  // parameter.
  //
  // If we're cancelling a previous transition, we need to take steps to make
  // sure the control points will end up in the right state regardless of
  // whether they completed the previous transition. Specifically, if
  // subject_time isn't specified, we infer it here and supply the inferred
  // value to the control points, so there's no disagreement about its value.

  std::shared_ptr<TimelineTransition> pending_transition =
      pending_transition_.lock();
  if (pending_transition) {
    // A transition is pending - cancel it.
    pending_transition->Cancel();
  }

  if (timeline_transform->subject_time != kUnspecifiedTime) {
    // We're seeking, so we may not be at end-of-stream anymore. The control
    // sites will signal end-of-stream again if we are.
    end_of_stream_ = false;
  }

  // These will be recorded as part of the new TimelineFunction.
  int64_t reference_time =
      timeline_transform->reference_time == kUnspecifiedTime
          ? (Timeline::local_now() + kDefaultLeadTime)
          : timeline_transform->reference_time;
  int64_t subject_time = timeline_transform->subject_time;

  // Determine the actual subject time, inferring it if it wasn't specified.
  int64_t actual_subject_time = subject_time == kUnspecifiedTime
                                    ? current_timeline_function_(reference_time)
                                    : subject_time;

  if (pending_transition && subject_time == kUnspecifiedTime) {
    // We're cancelling a pending transition, which may have already completed
    // at one or more of the control sites. We don't want the sites to have to
    // infer the subject_time, because we can't be sure what subject_time a
    // site will infer.
    subject_time = actual_subject_time;
  }

  // Record the new pending transition.
  std::shared_ptr<TimelineTransition> transition =
      std::shared_ptr<TimelineTransition>(
          new TimelineTransition(reference_time, actual_subject_time,
                                 timeline_transform->reference_delta,
                                 timeline_transform->subject_delta, callback));

  pending_transition_ = transition;

  TimelineTransform transform_to_send;
  transform_to_send.reference_time = reference_time;
  transform_to_send.subject_time = subject_time;
  transform_to_send.reference_delta = timeline_transform->reference_delta;
  transform_to_send.subject_delta = timeline_transform->subject_delta;

  // Initiate the transition for each control point.
  for (const std::unique_ptr<ControlPointState>& control_point_state :
       control_point_states_) {
    control_point_state->end_of_stream_ = false;
    control_point_state->consumer_->SetTimelineTransform(
        transform_to_send.Clone(), transition->NewCallback());
  }

  // If and when this transition is complete, adopt the new TimelineFunction
  // and tell any status subscribers.
  transition->WhenCompleted([this, transition]() {
    current_timeline_function_ = transition->new_timeline_function();
    status_publisher_.SendUpdates();
  });
}

void MediaTimelineControllerImpl::HandleControlPointEndOfStreamChange() {
  bool end_of_stream = true;
  for (const std::unique_ptr<ControlPointState>& control_point_state :
       control_point_states_) {
    if (!control_point_state->end_of_stream_) {
      end_of_stream = false;
      break;
    }
  }

  if (end_of_stream_ != end_of_stream) {
    end_of_stream_ = end_of_stream;
    status_publisher_.SendUpdates();
  }
}

MediaTimelineControllerImpl::ControlPointState::ControlPointState(
    MediaTimelineControllerImpl* parent,
    MediaTimelineControlPointPtr point)
    : parent_(parent), control_point_(point.Pass()) {
  control_point_->GetTimelineConsumer(GetProxy(&consumer_));
}

MediaTimelineControllerImpl::ControlPointState::~ControlPointState() {}

void MediaTimelineControllerImpl::ControlPointState::HandleStatusUpdates(
    uint64_t version,
    MediaTimelineControlPointStatusPtr status) {
  if (status) {
    // Respond to any end-of-stream changes.
    if (end_of_stream_ != status->end_of_stream) {
      end_of_stream_ = status->end_of_stream;
      parent_->HandleControlPointEndOfStreamChange();
    }
  }

  control_point_->GetStatus(
      version,
      [this](uint64_t version, MediaTimelineControlPointStatusPtr status) {
        HandleStatusUpdates(version, status.Pass());
      });
}

MediaTimelineControllerImpl::TimelineTransition::TimelineTransition(
    int64_t reference_time,
    int64_t subject_time,
    uint32_t reference_delta,
    uint32_t subject_delta,
    const SetTimelineTransformCallback& callback)
    : new_timeline_function_(reference_time,
                             subject_time,
                             reference_delta,
                             subject_delta),
      callback_(callback) {
  DCHECK(!callback_.is_null());
  callback_joiner_.WhenJoined([this]() {
    if (cancelled_) {
      DCHECK(callback_.is_null());
      return;
    }

    DCHECK(!callback_.is_null());
    callback_.Run(true);
    callback_.reset();
    if (!completed_callback_.is_null()) {
      completed_callback_.Run();
      completed_callback_.reset();
    }
  });
}

MediaTimelineControllerImpl::TimelineTransition::~TimelineTransition() {}

}  // namespace media
}  // namespace mojo
