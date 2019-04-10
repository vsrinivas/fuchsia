// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_NODES_VIEW_NODE_H_
#define GARNET_LIB_UI_GFX_RESOURCES_NODES_VIEW_NODE_H_

#include "garnet/lib/ui/gfx/resources/nodes/node.h"
#include "garnet/lib/ui/gfx/resources/view.h"

namespace scenic_impl {
namespace gfx {

// The |View| "phantom" node. This node is owned by a View and is
// used to connect a View to the scene graph. It can only be parented
// by the |ViewHolder|, and serves as the local root to the View's
// subtree.
class ViewNode final : public Node {
 public:
  static const ResourceTypeInfo kTypeInfo;

  // |Resource|
  void Accept(class ResourceVisitor* visitor) override;

  // |Node|
  ViewPtr FindOwningView() const override;

  // Returns pointer to the View that owns this node.
  View* GetView() const { return FindOwningView().get(); }

 private:
  friend class View;
  ViewNode(Session* session, ResourceId view_id);

  // The ID of the View owning this ViewNode.
  ResourceId view_id_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_RESOURCES_NODES_VIEW_NODE_H_
