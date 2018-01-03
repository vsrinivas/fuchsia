// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/view_manager/input/input_dispatcher_impl.h"

#include <queue>
#include "lib/ui/geometry/cpp/geometry_util.h"
#include "lib/ui/input/cpp/formatting.h"
#include "lib/ui/views/cpp/formatting.h"
#include "garnet/bin/ui/view_manager/internal/input_owner.h"
#include "garnet/bin/ui/view_manager/internal/view_inspector.h"
#include "lib/escher/scene/camera.h"
#include "lib/escher/util/type_utils.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fsl/tasks/message_loop.h"

namespace view_manager {
namespace {

// Returns a pair of points representing a ray's origin and direction, in that
// order. The ray is constructed to point directly into the scene at the
// provided device coordinate.
std::pair<mozart::Point3F, mozart::Point3F> DefaultRayForHitTestingScreenPoint(
    const mozart::PointF& point) {
  mozart::Point3F origin;
  origin.x = point.x;
  origin.y = point.y;
  origin.z = -1.f;
  mozart::Point3F direction;
  direction.z = 1.f;
  return {origin, direction};
}

// Converts a mozart::Transform into a escher::mat4 suitable for use in
// mathematical operations.
escher::mat4 Unwrap(const mozart::Transform& matrix) {
  const auto& in = matrix.matrix;
  return {in[0], in[4], in[8],  in[12], in[1], in[5], in[9],  in[13],
          in[2], in[6], in[10], in[14], in[3], in[7], in[11], in[15]};
}

// Transforms the provided input event into the local coordinate system of the
// view associated with the event.
//
// This transformation makes several assumptions:
//   * The ray must be the same as the one passed to |inspector_|'s hit test,
//     which determined the originally hit view.
//   * For MOVE and up UP, which don't go through hit testing, the distance
//     is pinned to whatever distance the original hit occurred at. The origin
//     of the ray is the only thing that is shifted relative to the DOWN event.
//
// |ray_origin| is the origin of the ray in the device coordinate space.
// |ray_direction| is the direction of the ray in the device coordinate space.
// |transform| is the transform from the hit node's local coordinate space into
// the coordinate space of the ray.
// |distance| is the distance along the ray that the original hit occured.
// |event| is the event to transform.
void TransformPointerEvent(const mozart::Point3F& ray_origin,
                           const mozart::Point3F& ray_direction,
                           const mozart::Transform& transform,
                           float distance,
                           mozart::InputEvent* event) {
  if (!event->is_pointer())
    return;
  const mozart::PointerEventPtr& pointer = event->get_pointer();

  escher::mat4 hit_node_to_device_transform = Unwrap(transform);
  escher::ray4 ray{{ray_origin.x, ray_origin.y, ray_origin.z, 1.f},
                   {ray_direction.x, ray_direction.y, ray_direction.z, 0.f}};
  escher::ray4 transformed_ray =
      glm::inverse(hit_node_to_device_transform) * ray;

  escher::vec4 hit = escher::homogenize(transformed_ray.origin +
                                        distance * transformed_ray.direction);

  pointer->x = hit[0];
  pointer->y = hit[1];
}

// The input event fidl is currently defined to expect some number
// of milliseconds.
int64_t InputEventTimestampNow() {
  return fxl::TimePoint::Now().ToEpochDelta().ToNanoseconds();
}
}  // namespace

InputDispatcherImpl::InputDispatcherImpl(
    ViewInspector* inspector,
    InputOwner* owner,
    mozart::ViewTreeTokenPtr view_tree_token,
    fidl::InterfaceRequest<mozart::InputDispatcher> request)
    : inspector_(inspector),
      owner_(owner),
      view_tree_token_(std::move(view_tree_token)),
      binding_(this, std::move(request)),
      weak_factory_(this) {
  FXL_DCHECK(inspector_);
  FXL_DCHECK(view_tree_token_);

  binding_.set_error_handler(
      [this] { owner_->OnInputDispatcherDied(this); });
}

InputDispatcherImpl::~InputDispatcherImpl() {}

void InputDispatcherImpl::DispatchEvent(mozart::InputEventPtr event) {
  FXL_DCHECK(event);
  FXL_VLOG(1) << "DispatchEvent: " << *event;

  pending_events_.push(std::move(event));
  if (pending_events_.size() == 1u)
    ProcessNextEvent();
}

void InputDispatcherImpl::ProcessNextEvent() {
  FXL_DCHECK(!pending_events_.empty());

  do {
    const mozart::InputEvent* event = pending_events_.front().get();
    FXL_VLOG(1) << "ProcessNextEvent: " << *event;

    if (event->is_pointer()) {
      // TODO(MZ-164): We may also need to perform hit tests on ADD and
      // keep track of which views have seen the ADD or REMOVE so that
      // they can be balanced correctly.
      const mozart::PointerEventPtr& pointer = event->get_pointer();
      if (pointer->phase == mozart::PointerEvent::Phase::DOWN) {
        mozart::PointF point;
        point.x = pointer->x;
        point.y = pointer->y;
        FXL_VLOG(1) << "HitTest: point=" << point;
        std::pair<mozart::Point3F, mozart::Point3F> ray =
            DefaultRayForHitTestingScreenPoint(point);
        inspector_->HitTest(*view_tree_token_, ray.first, ray.second, [
          weak = weak_factory_.GetWeakPtr(), point
        ](std::vector<ViewHit> view_hits) mutable {
          if (weak)
            weak->OnHitTestResult(point, std::move(view_hits));
        });
        return;
      }
    } else if (event->is_keyboard()) {
      inspector_->ResolveFocusChain(
          view_tree_token_.Clone(),
          [weak = weak_factory_.GetWeakPtr()](
              std::unique_ptr<FocusChain> focus_chain) {
            if (weak) {
              // Make sure to keep processing events when no focus is defined
              if (focus_chain) {
                weak->OnFocusResult(std::move(focus_chain));
              } else {
                weak->PopAndScheduleNextEvent();
              }
            }
          });
      return;
    }
    DeliverEvent(std::move(pending_events_.front()));
    pending_events_.pop();
  } while (!pending_events_.empty());
}

void InputDispatcherImpl::DeliverEvent(uint64_t event_path_propagation_id,
                                       size_t index,
                                       mozart::InputEventPtr event) {
  // TODO(MZ-164) when the chain is changed, we might need to cancel events
  // that have not progagated fully through the chain.
  if (index >= event_path_.size() ||
      event_path_propagation_id_ != event_path_propagation_id)
    return;

  // TODO(MZ-33) once input arena is in place, we won't need the "handled"
  // boolean on the callback anymore.
  mozart::InputEventPtr view_event = event.Clone();
  const ViewHit& view_hit = event_path_[index];
  const mozart::PointerEventPtr& pointer = event->get_pointer();
  mozart::PointF point;
  point.x = pointer->x;
  point.y = pointer->y;
  std::pair<mozart::Point3F, mozart::Point3F> ray =
      DefaultRayForHitTestingScreenPoint(point);
  TransformPointerEvent(ray.first, ray.second, *view_hit.inverse_transform,
                        view_hit.distance, view_event.get());
  FXL_VLOG(1) << "DeliverEvent " << event_path_propagation_id << " to "
              << event_path_[index].view_token << ": " << *view_event;
  owner_->DeliverEvent(
      &event_path_[index].view_token, std::move(view_event), fxl::MakeCopyable([
        this, event_path_propagation_id, index, event = std::move(event)
      ](bool handled) mutable {
        if (!handled) {
          DeliverEvent(event_path_propagation_id, index + 1, std::move(event));
        }
      }));
}

void InputDispatcherImpl::DeliverEvent(mozart::InputEventPtr event) {
  DeliverEvent(event_path_propagation_id_, 0u, std::move(event));
}

void InputDispatcherImpl::DeliverKeyEvent(
    std::unique_ptr<FocusChain> focus_chain,
    uint64_t propagation_index,
    mozart::InputEventPtr event) {
  FXL_DCHECK(propagation_index < focus_chain->chain.size());
  FXL_VLOG(1) << "DeliverKeyEvent " << focus_chain->version << " "
              << (1 + propagation_index) << "/" << focus_chain->chain.size()
              << " " << *(focus_chain->chain[propagation_index]) << ": "
              << *event;

  auto cloned_event = event.Clone();
  owner_->DeliverEvent(
      focus_chain->chain[propagation_index].get(), std::move(event),
      fxl::MakeCopyable([
        this, focus_chain = std::move(focus_chain), propagation_index,
        cloned_event = std::move(cloned_event)
      ](bool handled) mutable {
        FXL_VLOG(2) << "Event " << *cloned_event << (handled ? "" : " Not")
                    << " Handled by "
                    << *(focus_chain->chain[propagation_index]);

        if (!handled && propagation_index + 1 < focus_chain->chain.size()) {
          // Avoid re-entrance on DeliverKeyEvent
          fsl::MessageLoop::GetCurrent()->task_runner()->PostTask(
              fxl::MakeCopyable([
                weak = weak_factory_.GetWeakPtr(),
                focus_chain = std::move(focus_chain), propagation_index,
                cloned_event = std::move(cloned_event)
              ]() mutable {
                FXL_VLOG(2) << "Propagating event to "
                            << *(focus_chain->chain[propagation_index + 1]);

                if (weak)
                  weak->DeliverKeyEvent(std::move(focus_chain),
                                        propagation_index + 1,
                                        std::move(cloned_event));

              }));
        }
      }));
}

void InputDispatcherImpl::PopAndScheduleNextEvent() {
  if (!pending_events_.empty()) {
    pending_events_.pop();
    if (!pending_events_.empty()) {
      // Prevent reentrance from ProcessNextEvent.
      auto process_next_event = [weak = weak_factory_.GetWeakPtr()] {
        if (weak)
          weak->ProcessNextEvent();
      };
      fsl::MessageLoop::GetCurrent()->task_runner()->PostTask(
          process_next_event);
    }
  }
}

void InputDispatcherImpl::OnFocusResult(
    std::unique_ptr<FocusChain> focus_chain) {
  FXL_VLOG(1) << "OnFocusResult " << focus_chain->version << " "
              << focus_chain->chain.size();
  if (focus_chain->chain.size() > 0) {
    DeliverKeyEvent(std::move(focus_chain), 0,
                    std::move(pending_events_.front()));
  }
  PopAndScheduleNextEvent();
}

void InputDispatcherImpl::OnHitTestResult(const mozart::PointF& point,
                                          std::vector<ViewHit> view_hits) {
  FXL_DCHECK(!pending_events_.empty());

  if (view_hits.empty()) {
    PopAndScheduleNextEvent();
    return;
  }

  // FIXME(jpoichet) This should be done somewhere else.
  inspector_->ActivateFocusChain(
      view_hits.front().view_token.Clone(),
      [this](std::unique_ptr<FocusChain> new_chain) {
        if (!active_focus_chain_ ||
            active_focus_chain_->chain.front()->value !=
                new_chain->chain.front()->value) {
          if (active_focus_chain_) {
            FXL_VLOG(1) << "Input focus lost by "
                        << *(active_focus_chain_->chain.front().get());
            mozart::InputEventPtr event = mozart::InputEvent::New();
            mozart::FocusEventPtr focus = mozart::FocusEvent::New();
            focus->event_time = InputEventTimestampNow();
            focus->focused = false;
            event->set_focus(std::move(focus));
            owner_->DeliverEvent(active_focus_chain_->chain.front().get(),
                                 std::move(event), nullptr);
          }

          FXL_VLOG(1) << "Input focus gained by "
                      << *(new_chain->chain.front().get());
          mozart::InputEventPtr event = mozart::InputEvent::New();
          mozart::FocusEventPtr focus = mozart::FocusEvent::New();
          focus->event_time = InputEventTimestampNow();
          focus->focused = true;
          event->set_focus(std::move(focus));
          owner_->DeliverEvent(new_chain->chain.front().get(), std::move(event),
                               nullptr);

          active_focus_chain_ = std::move(new_chain);
        }
      });

  // TODO(jpoichet) Implement Input Arena
  event_path_propagation_id_++;
  event_path_ = std::move(view_hits);

  FXL_VLOG(1) << "OnViewHitResolved: view_token_="
              << event_path_.front().view_token
              << ", view_transform_=" << event_path_.front().inverse_transform
              << ", event_path_propagation_id_=" << event_path_propagation_id_;

  DeliverEvent(std::move(pending_events_.front()));
  PopAndScheduleNextEvent();
}

}  // namespace view_manager
