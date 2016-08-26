// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_SERVICES_FACTORY_TIMELINE_CONTROLLER_IMPL_H_
#define APPS_MEDIA_SERVICES_FACTORY_TIMELINE_CONTROLLER_IMPL_H_

#include <memory>
#include <vector>

#include "apps/media/cpp/timeline.h"
#include "apps/media/cpp/timeline_function.h"
#include "apps/media/interfaces/timeline_controller.mojom.h"
#include "apps/media/services/common/mojo_publisher.h"
#include "apps/media/services/framework/util/callback_joiner.h"
#include "apps/media/services/media_service/media_service_impl.h"
#include "mojo/public/cpp/bindings/binding.h"

namespace mojo {
namespace media {

// Mojo agent that controls timing in a graph.
class MediaTimelineControllerImpl
    : public MediaServiceImpl::Product<MediaTimelineController>,
      public MediaTimelineController,
      public MediaTimelineControlPoint,
      public TimelineConsumer {
 public:
  static std::shared_ptr<MediaTimelineControllerImpl> Create(
      InterfaceRequest<MediaTimelineController> request,
      MediaServiceImpl* owner);

  ~MediaTimelineControllerImpl() override;

  // MediaTimelineController implementation.
  void AddControlPoint(
      InterfaceHandle<MediaTimelineControlPoint> control_point) override;

  void GetControlPoint(
      InterfaceRequest<MediaTimelineControlPoint> control_point) override;

  // MediaTimelineControlPoint implementation.
  void GetStatus(uint64_t version_last_seen,
                 const GetStatusCallback& callback) override;

  void GetTimelineConsumer(
      InterfaceRequest<TimelineConsumer> timeline_consumer) override;

  void Prime(const PrimeCallback& callback) override;

  // TimelineConsumer implementation.
  void SetTimelineTransform(
      TimelineTransformPtr timeline_transform,
      const SetTimelineTransformCallback& callback) override;

 private:
  static constexpr int64_t kDefaultLeadTime = Timeline::ns_from_ms(30);

  // Relationship to subordinate control point.
  struct ControlPointState {
    ControlPointState(MediaTimelineControllerImpl* parent,
                      MediaTimelineControlPointPtr control_point);

    ~ControlPointState();

    void HandleStatusUpdates(
        uint64_t version = MediaTimelineControlPoint::kInitialStatus,
        MediaTimelineControlPointStatusPtr status = nullptr);

    MediaTimelineControllerImpl* parent_;
    MediaTimelineControlPointPtr control_point_;
    TimelineConsumerPtr consumer_;
    bool end_of_stream_ = false;
  };

  class TimelineTransition
      : public std::enable_shared_from_this<TimelineTransition> {
   public:
    TimelineTransition(int64_t reference_time,
                       int64_t subject_time,
                       uint32_t reference_delta,
                       uint32_t subject_delta,
                       const SetTimelineTransformCallback& callback);

    ~TimelineTransition();

    // Calls returns a new callback for a child (control point) transition. THIS
    // METHOD WILL ONLY WORK IF THERE IS ALREADY A SHARED POINTER TO THIS
    // OBJECT.
    std::function<void(bool)> NewCallback() {
      callback_joiner_.Spawn();

      std::shared_ptr<TimelineTransition> this_ptr = shared_from_this();
      DCHECK(!this_ptr.unique());

      return [this_ptr](bool completed) {
        DCHECK(this_ptr);
        if (!completed && !this_ptr->cancelled_) {
          LOG(WARNING)
              << "A control point transition was cancelled unexpectedly.";
        }
        this_ptr->callback_joiner_.Complete();
      };
    }

    // Cancels this transition.
    void Cancel() {
      DCHECK(!cancelled_);
      cancelled_ = true;
      DCHECK(callback_.is_null());
      callback_.Run(false);
      callback_.reset();
      completed_callback_.reset();
    }

    // Specifies a callback to be called if and when the transition is complete.
    // The callback will never be called if the transition is cancelled.
    void WhenCompleted(const mojo::Callback<void()>& completed_callback) {
      DCHECK(completed_callback_.is_null());
      if (callback_.is_null() && !cancelled_) {
        completed_callback.Run();
      } else {
        completed_callback_ = completed_callback;
      }
    }

    // Returns the TimelineFunction that will result from this transition.
    const TimelineFunction& new_timeline_function() const {
      return new_timeline_function_;
    }

   private:
    TimelineFunction new_timeline_function_;
    SetTimelineTransformCallback callback_;
    CallbackJoiner callback_joiner_;
    bool cancelled_ = false;
    Callback<void()> completed_callback_;
  };

  MediaTimelineControllerImpl(InterfaceRequest<MediaTimelineController> request,
                              MediaServiceImpl* owner);

  // Takes action when a control point changes its end-of-stream value.
  void HandleControlPointEndOfStreamChange();

  Binding<MediaTimelineControlPoint> control_point_binding_;
  Binding<TimelineConsumer> consumer_binding_;
  MojoPublisher<GetStatusCallback> status_publisher_;
  std::vector<std::unique_ptr<ControlPointState>> control_point_states_;
  TimelineFunction current_timeline_function_;
  bool end_of_stream_ = false;
  std::weak_ptr<TimelineTransition> pending_transition_;
};

}  // namespace media
}  // namespace mojo

#endif  // APPS_MEDIA_SERVICES_FACTORY_TIMELINE_CONTROLLER_IMPL_H_
