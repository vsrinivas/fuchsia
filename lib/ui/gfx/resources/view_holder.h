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
#include "lib/ui/scenic/types.h"

namespace scenic {
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

  ViewHolder(Session* session, scenic::ResourceId id,
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

  // Refresh the cached scene by reading it from our parent.
  // Used when the parent is changed via SetParent but also called externally
  // e.g. when the parent of our parent has changed.
  void RefreshScene();

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

  ViewLinker::ExportLink link_;
  Scene* scene_ = nullptr;
  View* view_ = nullptr;
  Node* parent_ = nullptr;
  fuchsia::ui::gfx::ViewProperties view_properties_;
};
using ViewHolderPtr = fxl::RefPtr<ViewHolder>;

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_RESOURCES_VIEW_HOLDER_H_
