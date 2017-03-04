// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/input_manager/input_dispatcher_impl.h"

#include <queue>

#include "apps/mozart/services/composition/cpp/formatting.h"
#include "apps/mozart/services/geometry/cpp/geometry_util.h"
#include "apps/mozart/services/input/cpp/formatting.h"
#include "apps/mozart/services/views/cpp/formatting.h"
#include "apps/mozart/src/input_manager/input_associate.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/mtl/tasks/message_loop.h"

namespace input_manager {
namespace {
void TransformEvent(const mozart::Transform& transform,
                    mozart::InputEvent* event) {
  if (!event->is_pointer())
    return;
  const mozart::PointerEventPtr& pointer = event->get_pointer();
  mozart::PointF point;
  point.x = pointer->x;
  point.y = pointer->y;
  point = TransformPoint(transform, point);
  pointer->x = point.x;
  pointer->y = point.y;
}

// The input event fidl is currently defined to expect some number
// of milliseconds.
int64_t InputEventTimestampNow() {
  return ftl::TimePoint::Now().ToEpochDelta().ToNanoseconds();
}
}  // namespace

InputDispatcherImpl::InputDispatcherImpl(
    InputAssociate* associate,
    mozart::ViewTreeTokenPtr view_tree_token,
    fidl::InterfaceRequest<mozart::InputDispatcher> request)
    : associate_(associate),
      view_tree_token_(std::move(view_tree_token)),
      hit_tester_(ftl::MakeRefCounted<mozart::ViewTreeHitTesterClient>(
          associate_->inspector(),
          view_tree_token_.Clone())),
      view_hit_resolver_(new ViewHitResolver(associate_)),
      binding_(this, std::move(request)),
      weak_factory_(this) {
  FTL_DCHECK(associate_);
  FTL_DCHECK(view_tree_token_);

  binding_.set_connection_error_handler(
      [this] { associate_->OnInputDispatcherDied(this); });
}

InputDispatcherImpl::~InputDispatcherImpl() {}

void InputDispatcherImpl::DispatchEvent(mozart::InputEventPtr event) {
  FTL_DCHECK(event);

  pending_events_.push(std::move(event));
  if (pending_events_.size() == 1u)
    ProcessNextEvent();
}

void InputDispatcherImpl::ProcessNextEvent() {
  FTL_DCHECK(!pending_events_.empty());

  do {
    const mozart::InputEvent* event = pending_events_.front().get();
    if (event->is_pointer()) {
      const mozart::PointerEventPtr& pointer = event->get_pointer();
      if (pointer->phase == mozart::PointerEvent::Phase::DOWN) {
        auto point = mozart::PointF::New();
        point->x = pointer->x;
        point->y = pointer->y;
        FTL_VLOG(1) << "HitTest: point=" << point;
        auto hit_result_callback = ftl::MakeCopyable([
          pt = point.Clone(), weak = weak_factory_.GetWeakPtr()
        ](std::unique_ptr<mozart::ResolvedHits> resolved_hits) mutable {
          if (weak)
            weak->OnHitTestResult(std::move(pt), std::move(resolved_hits));
        });
        hit_tester_->HitTest(std::move(point), hit_result_callback);
        return;
      }
    }
    DeliverEvent(std::move(pending_events_.front()));
    pending_events_.pop();
  } while (!pending_events_.empty());
}

void InputDispatcherImpl::DeliverEvent(uint64_t event_path_propagation_id,
                                       const EventPath* chain,
                                       mozart::InputEventPtr event) {
  FTL_VLOG(1) << "DeliverEvent " << event_path_propagation_id << " " << *event;
  // TODO(jpoichet) when the chain is changed, we might need to cancel events
  // that have not progagated fully through the chain.
  if (chain && event_path_propagation_id_ == event_path_propagation_id) {
    FTL_DCHECK(chain->token);
    FTL_DCHECK(chain->transform);
    mozart::InputEventPtr cloned_event = event.Clone();
    // TODO(jpoichet) once input arena is in place, we won't need the "handled"
    // boolean on the callback anymore.
    associate_->DeliverEvent(
        chain->token.get(), std::move(event), ftl::MakeCopyable([
          this, event_path_propagation_id, chain, evt = std::move(cloned_event)
        ](bool handled) mutable {
          if (!handled &&
              event_path_propagation_id_ == event_path_propagation_id &&
              chain && chain->next) {
            // Move up the focus chain if not handled
            DeliverEvent(event_path_propagation_id, chain->next.get(),
                         std::move(evt));
          }
        }));
  }
}

void InputDispatcherImpl::DeliverEvent(mozart::InputEventPtr event) {
  if (event_path_) {
    TransformEvent(*(event_path_->transform), event.get());
    DeliverEvent(event_path_propagation_id_, event_path_.get(),
                 std::move(event));
  }
}

void InputDispatcherImpl::OnHitTestResult(
    mozart::PointFPtr point,
    std::unique_ptr<mozart::ResolvedHits> resolved_hits) {
  FTL_DCHECK(!pending_events_.empty());
  FTL_VLOG(1) << "OnHitTestResult: resolved_hits=" << resolved_hits.get();

  if (resolved_hits && resolved_hits->result()->root) {
    mozart::HitTestResultPtr result = resolved_hits->TakeResult();
    const mozart::SceneHit* root_scene = result->root.get();
    view_hit_resolver_->Resolve(
        root_scene, std::move(point), std::move(resolved_hits),
        [this](std::vector<std::unique_ptr<EventPath>> views) {
          if (views.empty()) {
            return;
          }

          bool focus_switched = true;
          if (event_path_) {
            if (event_path_->token->value != views.back()->token->value) {
              // Focus lost event
              mozart::InputEventPtr event = mozart::InputEvent::New();
              mozart::FocusEventPtr focus = mozart::FocusEvent::New();
              focus->event_time = InputEventTimestampNow();
              focus->focused = false;
              event->set_focus(std::move(focus));
              associate_->DeliverEvent(event_path_->token.get(),
                                       std::move(event), nullptr);

            } else {
              focus_switched = false;
            }
          }

          if (focus_switched) {
            // TODO(jpoichet) Implement Input Arena
            event_path_propagation_id_++;
            event_path_ = std::move(views.back());

            // Focus gained event
            mozart::InputEventPtr event = mozart::InputEvent::New();
            mozart::FocusEventPtr focus = mozart::FocusEvent::New();
            focus->event_time = InputEventTimestampNow();
            focus->focused = true;
            event->set_focus(std::move(focus));
            associate_->DeliverEvent(event_path_->token.get(), std::move(event),
                                     nullptr);
          }

          FTL_VLOG(1) << "OnViewHitResolved: view_token_=" << event_path_->token
                      << ", view_transform_=" << event_path_->transform
                      << ", event_path_propagation_id_="
                      << event_path_propagation_id_;

          DeliverEvent(std::move(pending_events_.front()));
          pending_events_.pop();

          if (!pending_events_.empty()) {
            // Prevent reentrance from ProcessNextEvent.
            auto process_next_event = [weak = weak_factory_.GetWeakPtr()] {
              if (weak)
                weak->ProcessNextEvent();
            };
            mtl::MessageLoop::GetCurrent()->task_runner()->PostTask(
                process_next_event);
          }
        });
  }
}

}  // namespace input_manager
