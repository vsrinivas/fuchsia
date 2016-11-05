// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/input_manager/input_dispatcher_impl.h"

#include "apps/mozart/glue/base/logging.h"
#include "apps/mozart/services/composition/cpp/formatting.h"
#include "apps/mozart/services/geometry/cpp/geometry_util.h"
#include "apps/mozart/services/views/cpp/formatting.h"
#include "apps/mozart/src/input_manager/input_associate.h"
#include "lib/mtl/tasks/message_loop.h"

namespace input_manager {
namespace {
void TransformEvent(const mozart::Transform& transform, mozart::Event* event) {
  if (!event->pointer_data)
    return;
  mozart::PointF point;
  point.x = event->pointer_data->x;
  point.y = event->pointer_data->y;
  point = TransformPoint(transform, point);
  event->pointer_data->x = point.x;
  event->pointer_data->y = point.y;
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
      binding_(this, std::move(request)),
      weak_factory_(this) {
  FTL_DCHECK(associate_);
  FTL_DCHECK(view_tree_token_);

  binding_.set_connection_error_handler(
      [this] { associate_->OnInputDispatcherDied(this); });
}

InputDispatcherImpl::~InputDispatcherImpl() {}

void InputDispatcherImpl::DispatchEvent(mozart::EventPtr event) {
  FTL_DCHECK(event);

  pending_events_.push(std::move(event));
  if (pending_events_.size() == 1u)
    ProcessNextEvent();
}

void InputDispatcherImpl::ProcessNextEvent() {
  FTL_DCHECK(!pending_events_.empty());

  do {
    const mozart::Event* event = pending_events_.front().get();
    if (event->action == mozart::EventType::POINTER_DOWN) {
      FTL_DCHECK(event->pointer_data);
      auto point = mozart::PointF::New();
      point->x = event->pointer_data->x;
      point->y = event->pointer_data->y;
      DVLOG(1) << "HitTest: point=" << point;
      auto hit_result_callback = [ this, weak = weak_factory_.GetWeakPtr() ](
          std::unique_ptr<mozart::ResolvedHits> resolved_hits) {
        if (weak)
          weak->OnHitTestResult(std::move(resolved_hits));
      };
      hit_tester_->HitTest(std::move(point), hit_result_callback);
      return;
    }
    DeliverEvent(std::move(pending_events_.front()));
    pending_events_.pop();
  } while (!pending_events_.empty());
}

void InputDispatcherImpl::DeliverEvent(mozart::EventPtr event) {
  if (focused_view_token_) {
    TransformEvent(*focused_view_transform_, event.get());
    associate_->DeliverEvent(focused_view_token_.get(), std::move(event));
  }
}

void InputDispatcherImpl::OnHitTestResult(
    std::unique_ptr<mozart::ResolvedHits> resolved_hits) {
  FTL_DCHECK(!pending_events_.empty());
  DVLOG(1) << "OnHitTestResult: resolved_hits=" << resolved_hits.get();

  // TODO(jeffbrown): Flesh out the input protocol so it makes sense to
  // look at more than the first hit.
  focused_view_token_.reset();
  focused_view_transform_.reset();
  if (resolved_hits && resolved_hits->result()->root) {
    mozart::HitTestResultPtr result = resolved_hits->TakeResult();
    const mozart::SceneHit* scene = result->root.get();
    for (;;) {
      FTL_DCHECK(scene->hits.size());
      if (scene->hits[0]->is_node()) {
        auto it = resolved_hits->map().find(scene->scene_token->value);
        if (it != resolved_hits->map().end()) {
          focused_view_token_ = it->second.Clone();
          focused_view_transform_ =
              std::move(scene->hits[0]->get_node()->transform);
        }
        break;
      }
      FTL_DCHECK(scene->hits[0]->is_scene());
      scene = scene->hits[0]->get_scene().get();
    }
  }

  DVLOG(1) << "OnHitTestResult: focused_view_token_=" << focused_view_token_
           << ", focused_view_transform_=" << focused_view_transform_;

  DeliverEvent(std::move(pending_events_.front()));
  pending_events_.pop();

  if (!pending_events_.empty()) {
    // Prevent reentrance from ProcessNextEvent.
    auto process_next_event = [weak = weak_factory_.GetWeakPtr()] {
      if (weak)
        weak->ProcessNextEvent();
    };
    mtl::MessageLoop::GetCurrent()->task_runner()->PostTask(process_next_event);
  }
}

}  // namespace input_manager
