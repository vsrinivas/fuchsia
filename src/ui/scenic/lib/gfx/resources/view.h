// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_VIEW_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_VIEW_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/zx/handle.h>

#include <optional>

#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/scenic/lib/gfx/engine/object_linker.h"
#include "src/ui/scenic/lib/gfx/engine/view_tree_updater.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/view_node.h"
#include "src/ui/scenic/lib/gfx/resources/resource.h"
#include "src/ui/scenic/lib/gfx/resources/resource_type_info.h"
#include "src/ui/scenic/lib/gfx/resources/resource_visitor.h"
#include "src/ui/scenic/lib/gfx/resources/view_holder.h"

namespace scenic_impl {
namespace gfx {

class Session;

// TODO(47147): Remove circular inclusion in View, ViewNode, ViewHolder and
// ViewTreeUpdater.
using ViewNodePtr = fxl::RefPtr<ViewNode>;
using ViewLinker = ObjectLinker<ViewHolder*, View*>;
using ViewPtr = fxl::RefPtr<View>;

// View and ViewHolder work together via the ViewLinker to allow scene
// traversal across Session boundaries.
//
// Once connected via their ImportLink and ExportLinks the View and
// ViewHolder will directly connect their child and parent Nodes.  This
// allows traversal to continue through them as if the View/ViewHolder were
// not present.  It works even if the View and ViewHolder are in separate
// processes!
//
// Disconnected Views do not participate in the scene graph in any way.  The
// link is only created once per View, so once a View is disconnected it may
// not be re-connected.
//
// Destroying the View will automatically disconnect the link if it is
// currently connected.
class View final : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  // TODO(fxbug.dev/24687):  The caller must ensure that |error_reporter| and |event_reporter|
  // outlive the constructed |View|.  Currently, these both have the same lifetime as |session|;
  // this invariant must be maintained.  However, it would be better to pass strong pointers.
  View(Session* session, ResourceId id, fuchsia::ui::views::ViewRefControl control_ref,
       fuchsia::ui::views::ViewRef view_ref, std::string debug_name,
       std::shared_ptr<ErrorReporter> error_reporter,
       fxl::WeakPtr<ViewTreeUpdater> view_tree_updater, EventReporterWeakPtr event_reporter);

  ~View() override;

  fxl::WeakPtr<View> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  // |Resource|
  void Accept(class ResourceVisitor* visitor) override;

  // Paired ViewHolder on the other side of the link.
  ViewHolder* view_holder() const { return view_holder_; }

  // Paired |ViewNode| used to attach this View and its children to the scene
  // graph.
  //
  // TODO(45371): This method is an implementation detail of View and should be
  // private.
  ViewNode* GetViewNode() const { return node_.get(); }

  // -- Manage Annotation ViewHolders --
  //
  // Insert |view_holder| to |annotation_view_holders_| and return true.
  // If it already exists, just return false.
  bool AddAnnotationViewHolder(ViewHolderPtr view_holder);

  // Remove the |view_holder| from |annotation_view_holders_| and return true.
  // If it doesn't exist, just return false.
  bool RemoveAnnotationViewHolder(ViewHolderPtr view_holder);

  const std::unordered_set<ViewHolderPtr>& annotation_view_holders() const {
    return annotation_view_holders_;
  }

  std::string debug_name() { return debug_name_; }

  // Connection management.  Call once the View is created to initiate the link
  // to its partner ViewHolder.
  void Connect(ViewLinker::ImportLink link);

  // Called by |ViewHolder| to set the handle of the render event. It is
  // triggered on the next render pass this View is involved in.
  void SetOnRenderEventHandle(zx_handle_t render_handle) { render_handle_ = render_handle; }
  // Called by |ViewHolder| to invalidate the event handle when the event is
  // closed.
  void InvalidateRenderEventHandle() { render_handle_ = ZX_HANDLE_INVALID; }
  // Called by the scenic render pass when this view's children are rendered
  // as part of a render frame.
  void SignalRender();

  void set_should_render_bounding_box(bool render_bounding_box) {
    should_render_bounding_box_ = render_bounding_box;
  }
  bool should_render_bounding_box() const { return should_render_bounding_box_; }

  // Accessor to this View's canonical ViewRef. Used to generate a FocusChain.
  const fuchsia::ui::views::ViewRef& view_ref() const { return view_ref_; }

  // Convenience accessor.
  zx_koid_t view_ref_koid() const;

 private:
  // TODO(46112): Remove friend usage.
  friend class ViewHolder;

  // |ViewLinker::ExportCallbacks|
  void LinkResolved(ViewHolder* view_holder);

  // |ViewLinker::ExportCallbacks|
  void LinkInvalidated(bool on_link_destruction);

  // -- Send / Broadcast View Events --
  //
  // Sends an event to its SessionListener only, but doesn't propagate to its
  // annotation ViewHolders.
  void SendViewHolderConnectedEvent();
  void SendViewHolderDisconnectedEvent();

  // Broadcast the event to its SessionListener and send the events to
  // annotation ViewHolders if they exist.
  void BroadcastViewPropertiesChangedEvent(fuchsia::ui::gfx::ViewProperties view_properties);

  // Callback function when annotation ViewHolder is destroyed.
  void OnAnnotationViewHolderDestroyed(ViewHolder* view_holder);

  std::optional<ViewLinker::ImportLink> link_;
  ViewHolder* view_holder_ = nullptr;

  std::unordered_set<ViewHolderPtr> annotation_view_holders_;

  // The View's "phantom node". This is the node corresponding to the View in
  // the scene graph. All parent-child relationships are through this node.
  // Note: this node should not be added to the Session's ResourceMap, and it's
  // lifetime is exclusively owned by this View.
  ViewNodePtr node_;

  // Handle signaled when any of this View's children are involved in a render
  // pass.
  zx_handle_t render_handle_;

  // Control_ref_ and view_ref_ are handles to an entangled eventpair.
  // Control_ref_ is the globally unique handle to one peer, and view_ref_ is
  // the cloneable handle to the other peer.
  fuchsia::ui::views::ViewRefControl control_ref_;
  fuchsia::ui::views::ViewRef view_ref_;
  const zx_koid_t view_ref_koid_ = ZX_KOID_INVALID;

  // Determines if view should render its bounding box and those of its embedded
  // view/view holders.
  bool should_render_bounding_box_ = false;

  const std::shared_ptr<ErrorReporter> error_reporter_;
  const EventReporterWeakPtr event_reporter_;

  fxl::WeakPtr<ViewTreeUpdater> view_tree_updater_;

  std::string debug_name_;

  fxl::WeakPtrFactory<View> weak_factory_;  // must be last
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_VIEW_H_
