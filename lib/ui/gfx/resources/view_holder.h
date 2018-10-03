// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_VIEW_HOLDER_H_
#define GARNET_LIB_UI_GFX_RESOURCES_VIEW_HOLDER_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <lib/fit/function.h>

#include "garnet/lib/ui/gfx/engine/object_linker.h"
#include "garnet/lib/ui/gfx/resources/resource.h"
#include "garnet/lib/ui/gfx/resources/resource_type_info.h"
#include "garnet/lib/ui/gfx/resources/resource_visitor.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace scenic_impl {
namespace gfx {

class Session;

class Node;
class Scene;
class View;
class ViewHolder;

using NodePtr = fxl::RefPtr<Node>;
using ViewLinker = ObjectLinker<ViewHolder, View>;

// ViewHolder and View work together via the ViewLinker to allow scene traversal
// across Session boundaries.
//
// Once connected via their ImportLink and ExportLinks the ViewHolder and View
// will directly connect their child and parent Nodes.  This allows traversal to
// continue through them as if the ViewHolder/View were not present.  It works
// even if the ViewHolder and View are in separate processes!
//
// Disconnected ViewHolders do not participate in the scene graph in any way.
// The link is only created once per ViewHolder, so once a ViewHolder is
// disconnected it may not be re-connected.
//
// Destroying the ViewHolder will automatically disconnect the link if it is
// currently connected.
class ViewHolder final : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  ViewHolder(Session* session, ResourceId id,
             ViewLinker::ExportLink link);
  ~ViewHolder() override;

  // | Resource |
  void Accept(class ResourceVisitor* visitor) override;
  bool Detach() override;

  // Paired View on the other side of the link.  This should be nullptr iff.
  // connected() is false.
  View* view() const { return view_; }

  // Parent management.  Use SetParent(nullptr) to detach this ViewHolder from
  // its current parent.
  void SetParent(Node* parent);
  Node* parent() const { return parent_; }

  // Connection management.  Call once the ViewHolder is created to initiate the
  // link to its partner View.
  void Connect();
  bool connected() const { return link_.initialized(); }

  // ViewProperties management.
  void SetViewProperties(fuchsia::ui::gfx::ViewProperties props);
  const fuchsia::ui::gfx::ViewProperties& view_properties() {
    return view_properties_;
  }

  // Called when the scene that this ViewHolder is attached to changes. The
  // ViewHolder's scene is determined from the ViewHolder's parent node.
  void RefreshScene();

  // Called when the corresponding View is traversed for rendering.
  void SetIsViewRendering(bool is_view_rendering);

 private:
  // | ViewLinker::ImportCallbacks |
  void LinkResolved(View* view);
  void LinkDisconnected();

  // Send an event to the child View's SessionListener.
  void SendViewPropertiesChangedEvent();
  void SendViewConnectedEvent();
  void SendViewDisconnectedEvent();
  void SendViewAttachedToSceneEvent();
  void SendViewDetachedFromSceneEvent();
  void SendViewStateChangedEvent();

  ViewLinker::ExportLink link_;
  View* view_ = nullptr;
  // The parent's scene. Cached here to detect when the scene the changes so
  // the ViewHolder can emit scene attached/detached events.
  Scene* scene_ = nullptr;
  // The parent Node of this ViewHolder. This node is inserted into the scene
  // graph.
  Node* parent_ = nullptr;

  fuchsia::ui::gfx::ViewProperties view_properties_;
  fuchsia::ui::gfx::ViewState view_state_;
};
using ViewHolderPtr = fxl::RefPtr<ViewHolder>;

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_RESOURCES_VIEW_HOLDER_H_
