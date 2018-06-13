// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_VIEW_HOLDER_H_
#define GARNET_LIB_UI_GFX_RESOURCES_VIEW_HOLDER_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <lib/fit/function.h>

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
class ViewHolder final : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  ViewHolder(Session* session, scenic::ResourceId id,
             ::fuchsia::ui::gfx::ViewHolderArgs args);
  ~ViewHolder() override;

  void Accept(class ResourceVisitor* visitor) override;

  // | ObjectLinker::E |
  void LinkResolved(ViewLinker* linker, View* child);
  void PeerDestroyed();
  void ConnectionClosed();

  void SetParent(NodePtr parent);
  const NodePtr& parent() { return parent_; }

  void SetViewProperties(fuchsia::ui::gfx::ViewProperties props);

 private:
  // Send an event to the view's SessionListener.
  void SendViewPropertiesChangedEvent();

  // TODO(SCN-794): The unofficial Scenic Style Guide sez:
  // "Strong-ref your children and weak-ref your parents".
  // Following this uniformly prevents reference cycles.
  NodePtr parent_;
  View* child_ = nullptr;
  ViewLinker::ObjectId export_handle_ = 0;
  fuchsia::ui::gfx::ViewProperties view_properties_;
};
using ViewHolderPtr = fxl::RefPtr<ViewHolder>;

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_RESOURCES_VIEW_HOLDER_H_
