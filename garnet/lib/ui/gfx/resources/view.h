// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_VIEW_H_
#define GARNET_LIB_UI_GFX_RESOURCES_VIEW_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/zx/handle.h>

#include "garnet/lib/ui/gfx/engine/object_linker.h"
#include "garnet/lib/ui/gfx/resources/nodes/view_node.h"
#include "garnet/lib/ui/gfx/resources/resource.h"
#include "garnet/lib/ui/gfx/resources/resource_type_info.h"
#include "garnet/lib/ui/gfx/resources/resource_visitor.h"
#include "garnet/lib/ui/gfx/resources/view_holder.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace scenic_impl {
namespace gfx {

class Session;

using ViewNodePtr = fxl::RefPtr<ViewNode>;
using ViewLinker = ObjectLinker<ViewHolder, View>;

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

  View(Session* session, ResourceId id, ViewLinker::ImportLink link);
  ~View() override;

  // |Resource|
  void Accept(class ResourceVisitor* visitor) override;

  // Paired ViewHolder on the other side of the link.  This should be nullptr
  // iff. connected() is false.
  ViewHolder* view_holder() const { return view_holder_; }
  // Paired |ViewNode| used to attach this View and its children to the scene
  // graph.
  ViewNode* GetViewNode() const { return node_.get(); }

  // Connection management.  Call once the View is created to initiate the link
  // to its partner ViewHolder.
  void Connect();
  bool connected() const { return link_.initialized(); }

  // Called by |ViewHolder| to set the handle of the render event. It is
  // triggered on the next render pass this View is involved in.
  void SetOnRenderEventHandle(zx_handle_t render_handle) {
    render_handle_ = render_handle;
  }
  // Called by |ViewHolder| to invalidate the event handle when the event is
  // closed.
  void InvalidateRenderEventHandle() { render_handle_ = ZX_HANDLE_INVALID; }
  // Called by the scenic render pass when this view's children are rendered
  // as part of a render frame.
  void SignalRender();

 private:
  // |ViewLinker::ExportCallbacks|
  void LinkResolved(ViewHolder* view_holder);
  void LinkDisconnected();

  // Sends an event to our SessionListener.
  void SendViewHolderConnectedEvent();
  void SendViewHolderDisconnectedEvent();

  ViewLinker::ImportLink link_;
  ViewHolder* view_holder_ = nullptr;

  // The View's "phantom node". This is the node corresponding to the View in
  // the scene graph. All parent-child relationships are through this node.
  // Note: this node should not be added to the Session's ResourceMap, and it's
  // lifetime is exclusively owned by this View.
  ViewNodePtr node_;

  // Handle signaled when any of this View's children are involved in a render
  // pass.
  zx_handle_t render_handle_;
};
using ViewPtr = fxl::RefPtr<View>;

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_RESOURCES_VIEW_H_
