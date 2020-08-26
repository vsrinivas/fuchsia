// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/view.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include "src/lib/fsl/handles/object_info.h"
#include "src/ui/scenic/lib/gfx/engine/engine.h"
#include "src/ui/scenic/lib/gfx/engine/hit_tester.h"
#include "src/ui/scenic/lib/gfx/engine/object_linker.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/node.h"
#include "src/ui/scenic/lib/gfx/resources/view_holder.h"
#include "src/ui/scenic/lib/gfx/util/validate_eventpair.h"
#include "src/ui/scenic/lib/scenic/event_reporter.h"

namespace scenic_impl {
namespace gfx {

namespace {

bool IsInputSuppressed(Node* node) {
  if (!node)
    return false;  // Checked all ancestors.

  if (node->hit_test_behavior() == fuchsia::ui::gfx::HitTestBehavior::kSuppress)
    return true;

  return IsInputSuppressed(node->parent());
}

}  // namespace

using fuchsia::ui::views::ViewHolderToken;
using fuchsia::ui::views::ViewRef;
using fuchsia::ui::views::ViewRefControl;

const ResourceTypeInfo View::kTypeInfo = {ResourceType::kView, "View"};

View::View(Session* session, ResourceId id, ViewRefControl control_ref, ViewRef view_ref,
           std::string debug_name, std::shared_ptr<ErrorReporter> error_reporter,
           fxl::WeakPtr<ViewTreeUpdater> view_tree_updater, EventReporterWeakPtr event_reporter)
    : Resource(session, session->id(), id, View::kTypeInfo),
      control_ref_(std::move(control_ref)),
      view_ref_(std::move(view_ref)),
      view_ref_koid_(fsl::GetKoid(view_ref_.reference.get())),
      error_reporter_(std::move(error_reporter)),
      event_reporter_(event_reporter),
      view_tree_updater_(view_tree_updater),
      debug_name_(debug_name),
      weak_factory_(this) {
  FX_DCHECK(error_reporter_);
  FX_DCHECK(view_ref_koid_ != ZX_KOID_INVALID);

  node_ = fxl::AdoptRef<ViewNode>(new ViewNode(session, session->id(), weak_factory_.GetWeakPtr()));

  {
    TRACE_DURATION_BEGIN("gfx", "ResourceCtorViewRefClone");
    ViewRef clone;
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

    fit::function<bool()> is_input_suppressed = [view_ptr = GetWeakPtr()] {
      if (!view_ptr)
        return false;
      return IsInputSuppressed(view_ptr->GetViewNode());
    };

    fit::function<std::optional<glm::mat4>()> global_transform = [weak_ptr = GetWeakPtr()] {
      // Return the global transform if the view is still alive and attached to a scene.
      return weak_ptr && weak_ptr->GetViewNode()->scene()
                 ? std::optional<glm::mat4>{weak_ptr->GetViewNode()->GetGlobalTransform()}
                 : std::nullopt;
    };

    fit::function<void(const escher::ray4& world_space_ray, HitAccumulator<ViewHit>* accumulator,
                       bool semantic_hit_test)>
        hit_test = [weak_ptr = GetWeakPtr()](const escher::ray4& world_space_ray,
                                             HitAccumulator<ViewHit>* accumulator,
                                             bool semantic_hit_test) {
          if (weak_ptr) {
            HitTest(weak_ptr->GetViewNode(), world_space_ray, accumulator, semantic_hit_test);
          }
        };

    fit::function<void(ViewHolderPtr)> create_callback =
        [weak_ptr = GetWeakPtr()](ViewHolderPtr annotation_view_holder) {
          FX_CHECK(weak_ptr);
          FX_DCHECK(annotation_view_holder);
          weak_ptr->AddAnnotationViewHolder(annotation_view_holder);

          // If View has valid properties, initialize ViewProperties for the
          // annotation ViewHolder, otherwise we will defer it until the View
          // is attached to Scene. We inherit the parent View's bounding box and
          // inset, but suppress all focus changes and hit testing behaviors.
          ViewHolder* view_holder = weak_ptr->view_holder();
          if (view_holder &&
              !fidl::Equals(view_holder->GetViewProperties(), fuchsia::ui::gfx::ViewProperties())) {
            auto annotation_view_properties = view_holder->GetViewProperties();
            annotation_view_properties.focus_change = false;
            annotation_view_holder->SetViewProperties(annotation_view_properties,
                                                      weak_ptr->error_reporter_.get());
          }
        };

    FX_DCHECK(session->id() != 0u) << "GFX-side invariant for ViewTree";
    if (view_tree_updater_) {
      view_tree_updater_->AddUpdate(
          ViewTreeNewRefNode{.view_ref = std::move(clone),
                             .event_reporter = std::move(reporter),
                             .may_receive_focus = std::move(may_receive_focus),
                             .is_input_suppressed = std::move(is_input_suppressed),
                             .global_transform = std::move(global_transform),
                             .hit_test = std::move(hit_test),
                             .add_annotation_view_holder = std::move(create_callback),
                             .session_id = session->id()});
    }
  }

  FX_DCHECK(validate_viewref(control_ref_, view_ref_));
}

View::~View() {
  if (view_tree_updater_) {
    view_tree_updater_->AddUpdate(ViewTreeDeleteNode({.koid = view_ref_koid_}));
  }

  // Explicitly detach the phantom node to ensure it is cleaned up.
  node_->Detach(error_reporter_.get());
}

void View::Connect(ViewLinker::ImportLink link) {
  FX_DCHECK(!link_);
  FX_DCHECK(link.valid());
  FX_DCHECK(!link.initialized());

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
  FX_DCHECK(!view_holder_);
  FX_DCHECK(view_holder);
  view_holder_ = view_holder;

  // Attaching our node to the holder should never fail.
  FX_CHECK(view_holder_->AddChild(node_, ErrorReporter::Default().get()))
      << "View::LinkResolved(): error while adding ViewNode as child of ViewHolder";

  SendViewHolderConnectedEvent();

  if (view_tree_updater_) {
    view_tree_updater_->AddUpdate(ViewTreeConnectToParent{
        .child = view_ref_koid_, .parent = view_holder_->view_holder_koid()});
  }
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

  if (view_tree_updater_) {
    view_tree_updater_->AddUpdate(ViewTreeDisconnectFromParent{.koid = view_ref_koid_});
  }
}

void View::SendViewHolderConnectedEvent() {
  if (event_reporter_) {
    fuchsia::ui::gfx::Event event;
    event.set_view_holder_connected({.view_id = id()});
    event_reporter_->EnqueueEvent(std::move(event));
  }
}

void View::SendViewHolderDisconnectedEvent() {
  if (event_reporter_) {
    fuchsia::ui::gfx::Event event;
    event.set_view_holder_disconnected({.view_id = id()});
    event_reporter_->EnqueueEvent(std::move(event));
  }
}

void View::BroadcastViewPropertiesChangedEvent(fuchsia::ui::gfx::ViewProperties view_properties) {
  // Update annotation ViewHolders' properties.
  // Focus changes are always suppressed.
  for (const ViewHolderPtr& annotation_view_holder : annotation_view_holders()) {
    auto new_annotation_view_properties = view_properties;
    new_annotation_view_properties.focus_change = false;
    annotation_view_holder->SetViewProperties(new_annotation_view_properties,
                                              error_reporter_.get());
  }
}

void View::OnAnnotationViewHolderDestroyed(ViewHolder* view_holder) {
  RemoveAnnotationViewHolder(fxl::Ref(view_holder));
}

bool View::AddAnnotationViewHolder(ViewHolderPtr view_holder) {
  if (annotation_view_holders_.find(view_holder) != annotation_view_holders_.end()) {
    return false;
  }
  // |view_holder| doesn't exist, add it to the set.
  GetViewNode()->AddChild(view_holder, error_reporter_.get());
  view_holder->SetOnDestroyedCallback(
      [view_weak_ptr = GetWeakPtr(), view_holder = view_holder.get()]() {
        // View may be destroyed earlier, so we check the validity of WeakPtr first.
        if (view_weak_ptr) {
          view_weak_ptr->OnAnnotationViewHolderDestroyed(view_holder);
        }
      });
  annotation_view_holders_.insert(std::move(view_holder));
  return true;
}

bool View::RemoveAnnotationViewHolder(ViewHolderPtr view_holder) {
  if (annotation_view_holders_.find(view_holder) == annotation_view_holders_.end()) {
    return false;
  }
  // |view_holder| exists, remove it from the set.
  view_holder->Detach(error_reporter_.get());
  annotation_view_holders_.erase(view_holder);
  return true;
}

}  // namespace gfx
}  // namespace scenic_impl
