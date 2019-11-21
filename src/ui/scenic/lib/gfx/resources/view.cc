// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/view.h"

#include <trace/event.h>

#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/gfx/engine/engine.h"
#include "src/ui/scenic/lib/gfx/engine/object_linker.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/node.h"
#include "src/ui/scenic/lib/gfx/util/validate_eventpair.h"
#include "src/ui/scenic/lib/scenic/event_reporter.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo View::kTypeInfo = {ResourceType::kView, "View"};

View::View(Session* session, ResourceId id, fuchsia::ui::views::ViewRefControl control_ref,
           fuchsia::ui::views::ViewRef view_ref, std::string debug_name,
           std::shared_ptr<ErrorReporter> error_reporter, EventReporter* event_reporter)
    : Resource(session, session->id(), id, View::kTypeInfo),
      control_ref_(std::move(control_ref)),
      view_ref_(std::move(view_ref)),
      view_ref_koid_(fsl::GetKoid(view_ref_.reference.get())),
      error_reporter_(std::move(error_reporter)),
      event_reporter_(event_reporter),
      gfx_session_(session),
      debug_name_(debug_name),
      weak_factory_(this) {
  FXL_DCHECK(error_reporter_);
  FXL_DCHECK(event_reporter_);
  FXL_DCHECK(view_ref_koid_ != ZX_KOID_INVALID);

  node_ = fxl::AdoptRef<ViewNode>(new ViewNode(session, session->id(), weak_factory_.GetWeakPtr()));

  {
    TRACE_DURATION_BEGIN("gfx", "ResourceCtorViewRefClone");
    fuchsia::ui::views::ViewRef clone;
    fidl::Clone(view_ref_, &clone);
    TRACE_DURATION_END("gfx", "ResourceCtorViewRefClone");

    EventReporterWeakPtr reporter = event_reporter->GetWeakPtr();

    fit::function<bool()> may_receive_focus = [view_ptr = GetWeakPtr()] {
      if (view_ptr && view_ptr->view_holder_) {
        return view_ptr->view_holder_->GetViewProperties().focus_change;
      }

      // By default, a view may receive focus.
      return true;
    };

    FXL_DCHECK(session->id() != 0u) << "GFX-side invariant for ViewTree";
    gfx_session_->view_tree_updates().push_back(
        ViewTreeNewRefNode{.view_ref = std::move(clone),
                           .event_reporter = std::move(reporter),
                           .may_receive_focus = std::move(may_receive_focus),
                           .gfx_session_id = session->id()});
  }

  FXL_DCHECK(validate_viewref(control_ref_, view_ref_));
}

View::~View() {
  gfx_session_->view_tree_updates().push_back(ViewTreeDeleteNode({.koid = view_ref_koid_}));

  // Explicitly detach the phantom node to ensure it is cleaned up.
  node_->Detach(error_reporter_.get());
}

void View::Connect(ViewLinker::ImportLink link) {
  FXL_DCHECK(!link_);
  FXL_DCHECK(link.valid());
  FXL_DCHECK(!link.initialized());

  link_ = std::move(link);
  link_->Initialize(fit::bind_member(this, &View::LinkResolved),
                    fit::bind_member(this, &View::LinkInvalidated));
}

void View::SignalRender() {
  if (!render_handle_) {
    return;
  }

  // Verify the render_handle_ is still valid before attempting to signal it.
  if (zx_object_get_info(render_handle_, ZX_INFO_HANDLE_VALID, /*buffer=*/NULL,
                         /*buffer_size=*/0, /*actual=*/NULL,
                         /*avail=*/NULL) == ZX_OK) {
    zx_status_t status = zx_object_signal(render_handle_, /*clear_mask=*/0u, ZX_EVENT_SIGNALED);
    ZX_ASSERT(status == ZX_OK);
  }
}

zx_koid_t View::view_ref_koid() const { return view_ref_koid_; }

void View::LinkResolved(ViewHolder* view_holder) {
  FXL_DCHECK(!view_holder_);
  FXL_DCHECK(view_holder);
  view_holder_ = view_holder;

  // Attaching our node to the holder should never fail.
  FXL_CHECK(view_holder_->AddChild(node_, ErrorReporter::Default().get()))
      << "View::LinkResolved(): error while adding ViewNode as child of ViewHolder";

  SendViewHolderConnectedEvent();

  gfx_session_->view_tree_updates().push_back(
      ViewTreeConnectToParent{.child = view_ref_koid_, .parent = view_holder_->view_holder_koid()});
}

void View::LinkInvalidated(bool on_link_destruction) {
  // The link is only destroyed when this View is being destroyed, and therefore all cleanup can
  // be skipped anyway.
  if (on_link_destruction) {
    return;
  }

  // The connection ViewHolder no longer exists, detach the phantom node from
  // the ViewHolder.
  node_->Detach(error_reporter_.get());

  view_holder_ = nullptr;
  // ViewHolder was disconnected. There are no guarantees on liveness of the
  // render event, so invalidate the handle.
  InvalidateRenderEventHandle();

  SendViewHolderDisconnectedEvent();

  gfx_session_->view_tree_updates().push_back(ViewTreeDisconnectFromParent{.koid = view_ref_koid_});
}

void View::SendViewHolderConnectedEvent() {
  fuchsia::ui::gfx::Event event;
  event.set_view_holder_connected({.view_id = id()});
  event_reporter_->EnqueueEvent(std::move(event));
}

void View::SendViewHolderDisconnectedEvent() {
  fuchsia::ui::gfx::Event event;
  event.set_view_holder_disconnected({.view_id = id()});
  event_reporter_->EnqueueEvent(std::move(event));
}

}  // namespace gfx
}  // namespace scenic_impl
