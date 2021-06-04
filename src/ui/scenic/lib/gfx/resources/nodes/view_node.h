// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_NODES_VIEW_NODE_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_NODES_VIEW_NODE_H_

#include "src/ui/scenic/lib/gfx/resources/nodes/node.h"
#include "src/ui/scenic/lib/gfx/resources/view.h"

namespace scenic_impl {
namespace gfx {

// The |View| "phantom" node. This node is owned by a View and is
// used to connect a View to the scene graph. It can only be parented
// by the |ViewHolder|, and serves as the local root to the View's
// subtree.
class ViewNode : public Node {
 public:
  static const ResourceTypeInfo kTypeInfo;

  // |Resource|
  void Accept(class ResourceVisitor* visitor) override;

  // |Node|
  ViewPtr FindOwningView() const override;

  // Returns pointer to the View that owns this node.
  View* GetView() const { return view_.get(); }

  // |Node|
  IntersectionInfo GetIntersection(const escher::ray4& ray,
                                   const IntersectionInfo& parent_intersection) const override;

  // Returns the bounding box supplied by the owning ViewHolder. Returns an empty bounding box if no
  // ViewHolder is available. Virtual for testing.
  virtual escher::BoundingBox GetBoundingBox() const;

  // Protected for testing.
 protected:
  friend class View;
  ViewNode(Session* session, SessionId session_id, fxl::WeakPtr<View> view);

 private:
  // The ID of the View owning this ViewNode.
  fxl::WeakPtr<View> view_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_NODES_VIEW_NODE_H_
