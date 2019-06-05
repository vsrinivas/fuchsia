// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_VIEW_HOLDER_H_
#define GARNET_LIB_UI_GFX_RESOURCES_VIEW_HOLDER_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fit/function.h>

#include "garnet/lib/ui/gfx/engine/object_linker.h"
#include "garnet/lib/ui/gfx/resources/nodes/node.h"
#include "garnet/lib/ui/gfx/resources/resource.h"
#include "garnet/lib/ui/gfx/resources/resource_type_info.h"
#include "garnet/lib/ui/gfx/resources/view.h"

namespace scenic_impl {
namespace gfx {

using ViewLinker = ObjectLinker<ViewHolder, View>;

// The public |ViewHolder| resource implemented as a Node. The |ViewHolder|
// and |View| classes are linked to communicate state and enable scene graph
// traversal across processes. The |ViewHolder| supports the public
// |ViewHolder| functionality, and is only able to add the linked View's
// |ViewNode| as a child.
class ViewHolder final : public Node {
 public:
  static const ResourceTypeInfo kTypeInfo;

  ViewHolder(Session* session, ResourceId node_id, ViewLinker::ExportLink link);
  ~ViewHolder() {}

  // |Resource|
  void Accept(class ResourceVisitor* visitor) override;
  // |Resource| ViewHolders don't support imports.
  void AddImport(Import* import) override {}
  void RemoveImport(Import* import) override {}

  // Connection management.  Call once the ViewHolder is created to initiate the
  // link to its partner View.
  void Connect();
  bool connected() const { return link_.initialized(); }

  // Paired View on the other side of the link.  This should be nullptr
  // iff. connected() is false.
  View* view() const { return view_; }

  // ViewProperties management.
  void SetViewProperties(fuchsia::ui::gfx::ViewProperties props);
  const fuchsia::ui::gfx::ViewProperties& GetViewProperties() {
    return view_properties_;
  }

 protected:
  // |Node|
  bool CanAddChild(NodePtr child_node) override;
  void OnSceneChanged() override;

 private:
  // |ViewLinker::ImportCallbacks|
  void LinkResolved(View* view);
  void LinkDisconnected();

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

  ViewLinker::ExportLink link_;
  View* view_ = nullptr;

  fuchsia::ui::gfx::ViewProperties view_properties_;
  fuchsia::ui::gfx::ViewState view_state_;
  // Event that is signaled when the corresponding View's children are rendered
  // by scenic.
  zx::event render_event_;
  // The waiter that is signaled when the View is involved in a render pass. The
  // wait is not set until after the View has connected, and is always cleared
  // in |LinkDisconnected|. The waiter must be destroyed before the event.
  async::Wait render_waiter_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_RESOURCES_VIEW_HOLDER_H_
