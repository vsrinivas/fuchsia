// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_VIEW_HOLDER_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_VIEW_HOLDER_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fit/function.h>
#include <zircon/types.h>

#include <optional>

#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/scenic/lib/gfx/engine/object_linker.h"
#include "src/ui/scenic/lib/gfx/engine/view_tree_updater.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/node.h"
#include "src/ui/scenic/lib/gfx/resources/resource.h"
#include "src/ui/scenic/lib/gfx/resources/resource_type_info.h"

namespace scenic_impl {
namespace gfx {

// TODO(47147): Remove circular inclusion in View, ViewNode, ViewHolder and
// ViewTreeUpdater.
using ViewHolderPtr = fxl::RefPtr<ViewHolder>;
using ViewLinker = ObjectLinker<ViewHolder*, View*>;

// The public |ViewHolder| resource implemented as a Node. The |ViewHolder|
// and |View| classes are linked to communicate state and enable scene graph
// traversal across processes. The |ViewHolder| supports the public
// |ViewHolder| functionality, and is only able to add the linked View's
// |ViewNode| as a child.
class ViewHolder final : public Node {
 public:
  static const ResourceTypeInfo kTypeInfo;

  ViewHolder(Session* session, SessionId session_id, ResourceId node_id, bool suppress_events,
             std::string debug_name, std::shared_ptr<ErrorReporter> error_reporter,
             fxl::WeakPtr<ViewTreeUpdater> view_tree_updater);
  ~ViewHolder() override;

  fxl::WeakPtr<ViewHolder> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  // |Resource|
  void Accept(class ResourceVisitor* visitor) override;

  // Connection management.  Call once the ViewHolder is created to initiate the
  // link to its partner View.
  void Connect(ViewLinker::ExportLink link);

  std::string debug_name() { return debug_name_; }

  // Paired View on the other side of the link.
  View* view() const { return view_; }

  // ViewProperties management.
  void SetViewProperties(fuchsia::ui::gfx::ViewProperties props, ErrorReporter* error_reporter);
  const fuchsia::ui::gfx::ViewProperties& GetViewProperties() { return view_properties_; }
  escher::BoundingBox GetLocalBoundingBox() const;

  // TODO(fxbug.dev/24681): Bounding boxes that are rotated by degrees that are not multiples
  // of 90 will cause the box to grow/shrink. This needs to be accounted for in the
  // rest of the code.
  escher::BoundingBox GetWorldBoundingBox() const;

  void set_bounds_color(glm::vec4 bounds_color) { bounds_color_ = bounds_color; }
  glm::vec4 bounds_color() const { return bounds_color_; }

  zx_koid_t view_holder_koid() const { return view_holder_koid_; }

  void SetOnDestroyedCallback(fit::function<void(void)> fn) { on_destroyed_ = std::move(fn); }

 protected:
  // |Node|
  bool CanAddChild(NodePtr child_node) override;
  void OnSceneChanged() override;

 private:
  // TODO(46112): Remove friend usage.
  friend class View;

  // |ViewLinker::ImportCallbacks|
  void LinkResolved(View* view);
  void LinkInvalidated(bool on_link_destruction);

  void ResetRenderEvent();
  void CloseRenderEvent();
  void SetIsViewRendering(bool is_view_rendering);

  // Send an event to the child View's SessionListener.
  void SendViewPropertiesChangedEvent();
  void SendViewConnectedEvent();
  void SendViewDisconnectedEvent();
  void SendViewAttachedToSceneEvent();
  void SendViewDetachedFromSceneEvent();
  void SendViewStateChangedEvent();

  std::optional<ViewLinker::ExportLink> link_;
  // Cache the link's endpoint ID (koid); it may get reset (and forgotten) over the link's
  // lifetime but is required to untrack the ViewHolder at destruction.
  zx_koid_t view_holder_koid_ = ZX_KOID_INVALID;
  View* view_ = nullptr;

  bool suppress_events_ = false;
  fit::function<void(void)> on_destroyed_;

  fuchsia::ui::gfx::ViewProperties view_properties_;
  fuchsia::ui::gfx::ViewState view_state_;
  bool should_render_bounding_box_ = false;
  glm::vec4 bounds_color_ = glm::vec4(1, 1, 1, 1);

  // Event that is signaled when the corresponding View's children are rendered
  // by scenic.
  zx::event render_event_;
  // The waiter that is signaled when the View is involved in a render pass. The
  // wait is not set until after the View has connected, and is always cleared
  // in |LinkDisconnected|. The waiter must be destroyed before the event.
  async::Wait render_waiter_;

  std::string debug_name_;

  const std::shared_ptr<ErrorReporter> error_reporter_;

  fxl::WeakPtr<ViewTreeUpdater> view_tree_updater_;

  fxl::WeakPtrFactory<ViewHolder> weak_factory_;  // must be last
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_VIEW_HOLDER_H_
