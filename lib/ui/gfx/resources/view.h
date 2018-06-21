// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_VIEW_H_
#define GARNET_LIB_UI_GFX_RESOURCES_VIEW_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <lib/fit/function.h>
#include <unordered_set>

#include "garnet/lib/ui/gfx/engine/object_linker.h"
#include "garnet/lib/ui/gfx/resources/nodes/node.h"
#include "garnet/lib/ui/gfx/resources/resource.h"
#include "garnet/lib/ui/gfx/resources/resource_type_info.h"
#include "garnet/lib/ui/gfx/resources/resource_visitor.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/ui/scenic/types.h"

namespace scenic {
namespace gfx {

class Session;
class View;
class ViewHolder;
using ViewLinker = ObjectLinker<View, ViewHolder>;

// View and ViewHolder work together to allow scene traversal
// to cross Session boundaries.  Once linked by an ObjectLinker,
// when traversal reaches a ViewHolder, it can proceed down
// through the child Nodes of the corresponding View.
class View final : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  View(Session* session, scenic::ResourceId id,
       ::fuchsia::ui::gfx::ViewArgs args);
  ~View() override;

  // | Resource |
  void Accept(class ResourceVisitor* visitor) override;

  // | ObjectLinker::I |
  void LinkResolved(ViewHolder* owner);
  void PeerDestroyed();
  void ConnectionClosed();

  void AddChild(NodePtr child_node);
  void DetachChild(Node* child_node);
  void DetachChildren();
  const std::unordered_set<NodePtr>& children() const { return children_; }

 private:
  std::unordered_set<NodePtr> children_;
  ViewHolder* owner_ = nullptr;
  ViewLinker::ObjectId import_handle_ = 0;
};
using ViewPtr = fxl::RefPtr<View>;

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_RESOURCES_VIEW_H_
