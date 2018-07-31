// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_VIEW_H_
#define GARNET_LIB_UI_GFX_RESOURCES_VIEW_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <lib/fit/function.h>
#include <unordered_set>

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
class View;
class ViewHolder;

using NodePtr = fxl::RefPtr<Node>;
using ViewLinker = ObjectLinker<ViewHolder, View>;

// View and ViewHolder work together via the ViewLinker to allow scene traversal
// across Session boundaries.
//
// Once connected via their ImportLink and ExportLinks the View and ViewHolder
// will directly connect their child and parent Nodes.  This allows traversal to
// continue through them as if the View/ViewHolder were not present.  It works
// even if the View and ViewHolder are in separate processes!
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

  View(Session* session, scenic::ResourceId id, ViewLinker::ImportLink link);
  ~View() override;

  // | Resource |
  void Accept(class ResourceVisitor* visitor) override;

  // Paired ViewHolder on the other side of the link.  This should be nullptr
  // iff. connected() is false.
  ViewHolder* view_holder() const { return view_holder_; }

  // Connection management.  Call once the View is created to initiate the link
  // to its partner ViewHolder.
  void Connect();
  bool connected() const { return link_.initialized(); }

  // Child Node management.
  bool AddChild(NodePtr child);
  void DetachChildren();
  const std::unordered_set<NodePtr>& children() const { return children_; }

 private:
  // | ViewLinker::ExportCallbacks |
  void LinkResolved(ViewHolder* view_holder);
  void LinkDisconnected();

  // Called by Node in order to notify the View when a child is removed.
  // TODO(SCN-820): Remove when parent-child relationships are split out of Node
  // and View.
  void RemoveChild(Node* child);

  // Send an event to the parent ViewHolder's SessionListener.
  void SendViewDisconnectedEvent();

  ViewLinker::ImportLink link_;
  ViewHolder* view_holder_ = nullptr;
  std::unordered_set<NodePtr> children_;

  // Used for |RemoveChild|.
  // TODO(SCN-820): Remove when parent-child relationships are split out of Node
  // and View.
  friend class Node;
};
using ViewPtr = fxl::RefPtr<View>;

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_RESOURCES_VIEW_H_
