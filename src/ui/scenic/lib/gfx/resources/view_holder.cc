// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/view_holder.h"

#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/types.h>

#include "src/lib/fsl/handles/object_info.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/resources/view.h"
#include "src/ui/scenic/lib/gfx/util/unwrap.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo ViewHolder::kTypeInfo = {ResourceType::kNode | ResourceType::kViewHolder,
                                                "ViewHolder"};

ViewHolder::ViewHolder(Session* session, SessionId session_id, ResourceId node_id,
                       bool suppress_events, std::string debug_name,
                       std::shared_ptr<ErrorReporter> error_reporter,
                       fxl::WeakPtr<ViewTreeUpdater> view_tree_updater)
    : Node(session, session_id, node_id, ViewHolder::kTypeInfo),
      suppress_events_(suppress_events),
      debug_name_(debug_name),
      error_reporter_(std::move(error_reporter)),
      view_tree_updater_(view_tree_updater),
      weak_factory_(this) {}

ViewHolder::~ViewHolder() {
  // Don't check if the link is valid since it may have been invalidated by the Import closing.
  // The view_holder_koid_ is the original value that was tracked anyway.
  if (link_ && view_tree_updater_) {
    view_tree_updater_->UntrackViewHolder(view_holder_koid_);
  }
}

void ViewHolder::Connect(ViewLinker::ExportLink link) {
  FX_DCHECK(!link_);
  FX_DCHECK(link.valid());
  FX_DCHECK(!link.initialized());

  link_ = std::move(link);
  view_holder_koid_ = link_->endpoint_id();
  if (view_tree_updater_) {
    view_tree_updater_->TrackViewHolder(GetWeakPtr());
  }
  link_->Initialize(fit::bind_member(this, &ViewHolder::LinkResolved),
                    fit::bind_member(this, &ViewHolder::LinkInvalidated));
}

void ViewHolder::LinkResolved(View* view) {
  // The view will also receive a LinkResolved call, and it will take care of
  // linking up the Nodes.
  FX_DCHECK(!view_ && view);
  view_ = view;

  if (!suppress_events_) {
    // Set the render waiting event on the view.
    ResetRenderEvent();

    SendViewConnectedEvent();
  }

  // If the ViewHolder is already attached to a scene, the linked view is now
  // also attached to the scene. Emit event.
  if (scene()) {
    SendViewAttachedToSceneEvent();
  }

  // This guarantees that the View is notified of any previously-set
  // ViewProperties.  Otherwise, e.g. if the ViewHolder properties were set
  // only once before the link was resolved, the View would never be notified.
  SendViewPropertiesChangedEvent();
  for (const auto& annotation : view_->annotation_view_holders()) {
    annotation->SendViewPropertiesChangedEvent();
  }
}

void ViewHolder::LinkInvalidated(bool on_link_destruction) {
  // The link is only destroyed when this ViewHolder is being destroyed, and therefore all cleanup
  // can be skipped anyway.
  if (on_link_destruction) {
    return;
  }

  // The child is already dead (or never existed) and it cleans things up in its
  // destructor, including Detaching any child Nodes.
  view_ = nullptr;

  if (!suppress_events_) {
    CloseRenderEvent();
    // Link was disconnected, the view can no longer be rendering. If the state
    // was previously rendering, update with not rendering event.
    SetIsViewRendering(false);

    SendViewDisconnectedEvent();
  }

  if (on_destroyed_) {
    on_destroyed_();
  }
}

// Generates an escher::BoundingBox from the given view properties.
// TODO(fxbug.dev/24680) Create internal ViewProperties type.
escher::BoundingBox ViewHolder::GetLocalBoundingBox() const {
  escher::vec3 min =
      Unwrap(view_properties_.bounding_box.min) + Unwrap(view_properties_.inset_from_min);
  escher::vec3 max =
      Unwrap(view_properties_.bounding_box.max) - Unwrap(view_properties_.inset_from_max);
  // Empty, point, or line view-bounds are permissible, but we collapse these to "empty".
  // In contrast, 2d surfaces (like rectangles) and 3d volumes (like cubes) are okay.
  return escher::BoundingBox::NewChecked(min, max, /* max degenerate dimension */ 1);
}

// Returns the world-space bounding box.
escher::BoundingBox ViewHolder::GetWorldBoundingBox() const {
  return GetGlobalTransform() * GetLocalBoundingBox();
}

void ViewHolder::SetViewProperties(fuchsia::ui::gfx::ViewProperties props,
                                   ErrorReporter* error_reporter) {
  if (!fidl::Equals(props, view_properties_)) {
    view_properties_ = std::move(props);
    // This code transforms the bounding box given to the view holder
    // into a set of clipping planes on the transform node that will
    // then be applied to all children of this view holder. This is
    // to ensure that all geometry gets clipped to the view bounds and
    // does not extend past its allowed extent.
    SetClipPlanesFromBBox(GetLocalBoundingBox(), error_reporter);
    SendViewPropertiesChangedEvent();
  }
}

void ViewHolder::OnSceneChanged() {
  if (scene()) {
    SendViewAttachedToSceneEvent();
  } else {
    // View is no longer part of a scene and therefore cannot render to one.
    SetIsViewRendering(false);
    // Reset the render event so that when the View is reattached to the scene
    // and its children render, this ViewHolder will get the signal.
    ResetRenderEvent();

    SendViewDetachedFromSceneEvent();
  }
}

bool ViewHolder::CanAddChild(NodePtr child_node) {
  // A ViewHolder can only have a child node that is associated with the
  // connected View.
  if (!(child_node->type_flags() & ResourceType::kViewNode)) {
    return false;
  }

  if (view_ && view_->GetViewNode()) {
    return view_->GetViewNode()->id() == child_node->id();
  }
  // else, no view set and so this cannot verify the child. Return false.
  // Note: the child of this node should only be added by View when the link
  // is between this ViewHolder and the View are connected.
  return false;
}

void ViewHolder::ResetRenderEvent() {
  if (!view_) {
    return;
  }

  // Close any previously set event.
  CloseRenderEvent();

  // Create a new render event.
  zx_status_t status = zx::event::create(0u, &render_event_);
  ZX_ASSERT(status == ZX_OK);
  // Re-arm the wait.
  render_waiter_.set_object(render_event_.get());
  render_waiter_.set_trigger(ZX_EVENT_SIGNALED);
  render_waiter_.set_handler(
      [this](async_dispatcher_t*, async::Wait*, zx_status_t status, const zx_packet_signal_t*) {
        ZX_ASSERT(status == ZX_OK || status == ZX_ERR_CANCELED);
        if (status == ZX_OK) {
          SetIsViewRendering(true);
        }

        // The first frame has been signaled. Clear the event as it is not used
        // for subsequent frames.
        CloseRenderEvent();
      });
  status = render_waiter_.Begin(async_get_default_dispatcher());
  ZX_ASSERT(status == ZX_OK);

  // Set the event on the View to signal when it is next rendered.
  view_->SetOnRenderEventHandle(render_event_.get());
}

void ViewHolder::CloseRenderEvent() {
  if (view_) {
    view_->InvalidateRenderEventHandle();
  }

  if (render_waiter_.is_pending()) {
    zx_status_t wait_status = render_waiter_.Cancel();
    ZX_ASSERT(wait_status == ZX_OK);
  }
  render_event_.reset();
}

void ViewHolder::SetIsViewRendering(bool is_rendering) {
  if (view_state_.is_rendering == is_rendering) {
    // No state change, return.
    return;
  }
  view_state_.is_rendering = is_rendering;
  SendViewStateChangedEvent();
}

void ViewHolder::SendViewPropertiesChangedEvent() {
  if (!view_) {
    return;
  }
  fuchsia::ui::gfx::Event event;
  event.set_view_properties_changed({.view_id = view_->id(), .properties = view_properties_});
  view_->event_reporter()->EnqueueEvent(std::move(event));
  view_->BroadcastViewPropertiesChangedEvent(view_properties_);
}

void ViewHolder::SendViewConnectedEvent() {
  fuchsia::ui::gfx::Event event;
  event.set_view_connected({.view_holder_id = id()});
  event_reporter()->EnqueueEvent(std::move(event));
}

void ViewHolder::SendViewDisconnectedEvent() {
  fuchsia::ui::gfx::Event event;
  event.set_view_disconnected({.view_holder_id = id()});
  event_reporter()->EnqueueEvent(std::move(event));
}

void ViewHolder::SendViewAttachedToSceneEvent() {
  if (!view_) {
    return;
  }
  fuchsia::ui::gfx::Event event;
  event.set_view_attached_to_scene({.view_id = view_->id(), .properties = view_properties_});
  view_->event_reporter()->EnqueueEvent(std::move(event));
}

void ViewHolder::SendViewDetachedFromSceneEvent() {
  if (!view_) {
    return;
  }
  fuchsia::ui::gfx::Event event;
  event.set_view_detached_from_scene({.view_id = view_->id()});
  view_->event_reporter()->EnqueueEvent(std::move(event));
}

void ViewHolder::SendViewStateChangedEvent() {
  fuchsia::ui::gfx::Event event;
  event.set_view_state_changed({.view_holder_id = id(), .state = view_state_});
  event_reporter()->EnqueueEvent(std::move(event));
}

}  // namespace gfx
}  // namespace scenic_impl
