// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/input_manager/input_dispatcher_impl.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "mojo/services/geometry/cpp/geometry_util.h"
#include "apps/mozart/services/composition/cpp/formatting.h"
#include "mojo/services/ui/views/cpp/formatting.h"
#include "services/ui/input_manager/input_associate.h"

namespace input_manager {
namespace {
void TransformEvent(const mojo::Transform& transform, mojo::Event* event) {
  if (!event->pointer_data)
    return;
  mojo::PointF point;
  point.x = event->pointer_data->x;
  point.y = event->pointer_data->y;
  point = TransformPoint(transform, point);
  event->pointer_data->x = point.x;
  event->pointer_data->y = point.y;
}
}  // namespace

InputDispatcherImpl::InputDispatcherImpl(
    InputAssociate* associate,
    mojo::ui::ViewTreeTokenPtr view_tree_token,
    mojo::InterfaceRequest<mojo::ui::InputDispatcher> request)
    : associate_(associate),
      view_tree_token_(view_tree_token.Pass()),
      hit_tester_(
          new mojo::ui::ViewTreeHitTesterClient(associate_->inspector(),
                                                view_tree_token_.Clone())),
      binding_(this, request.Pass()),
      weak_factory_(this) {
  DCHECK(associate_);
  DCHECK(view_tree_token_);

  binding_.set_connection_error_handler(
      base::Bind(&InputAssociate::OnInputDispatcherDied,
                 base::Unretained(associate_), base::Unretained(this)));
}

InputDispatcherImpl::~InputDispatcherImpl() {}

void InputDispatcherImpl::DispatchEvent(mojo::EventPtr event) {
  DCHECK(event);

  pending_events_.push(event.Pass());
  if (pending_events_.size() == 1u)
    ProcessNextEvent();
}

void InputDispatcherImpl::ProcessNextEvent() {
  DCHECK(!pending_events_.empty());

  do {
    const mojo::Event* event = pending_events_.front().get();
    if (event->action == mojo::EventType::POINTER_DOWN) {
      DCHECK(event->pointer_data);
      auto point = mojo::PointF::New();
      point->x = event->pointer_data->x;
      point->y = event->pointer_data->y;
      DVLOG(1) << "HitTest: point=" << point;
      hit_tester_->HitTest(point.Pass(),
                           base::Bind(&InputDispatcherImpl::OnHitTestResult,
                                      weak_factory_.GetWeakPtr()));
      return;
    }
    DeliverEvent(pending_events_.front().Pass());
    pending_events_.pop();
  } while (!pending_events_.empty());
}

void InputDispatcherImpl::DeliverEvent(mojo::EventPtr event) {
  if (focused_view_token_) {
    TransformEvent(*focused_view_transform_, event.get());
    associate_->DeliverEvent(focused_view_token_.get(), event.Pass());
  }
}

void InputDispatcherImpl::OnHitTestResult(
    scoped_ptr<mojo::ui::ResolvedHits> resolved_hits) {
  DCHECK(!pending_events_.empty());
  DVLOG(1) << "OnHitTestResult: resolved_hits=" << resolved_hits.get();

  // TODO(jeffbrown): Flesh out the input protocol so it makes sense to
  // look at more than the first hit.
  focused_view_token_.reset();
  focused_view_transform_.reset();
  if (resolved_hits && resolved_hits->result()->root) {
    mojo::gfx::composition::HitTestResultPtr result =
        resolved_hits->TakeResult();
    const mojo::gfx::composition::SceneHit* scene = result->root.get();
    for (;;) {
      DCHECK(scene->hits.size());
      if (scene->hits[0]->is_node()) {
        auto it = resolved_hits->map().find(scene->scene_token->value);
        if (it != resolved_hits->map().end()) {
          focused_view_token_ = it->second.Clone();
          focused_view_transform_ =
              scene->hits[0]->get_node()->transform.Pass();
        }
        break;
      }
      DCHECK(scene->hits[0]->is_scene());
      scene = scene->hits[0]->get_scene().get();
    }
  }

  DVLOG(1) << "OnHitTestResult: focused_view_token_=" << focused_view_token_
           << ", focused_view_transform_=" << focused_view_transform_;

  DeliverEvent(pending_events_.front().Pass());
  pending_events_.pop();

  if (!pending_events_.empty()) {
    // Prevent reentrance from ProcessNextEvent.
    base::MessageLoop::current()->PostTask(
        FROM_HERE, base::Bind(&InputDispatcherImpl::ProcessNextEvent,
                              weak_factory_.GetWeakPtr()));
  }
}

}  // namespace input_manager
