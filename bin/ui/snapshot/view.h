// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_SNAPSHOT_VIEW_H_
#define GARNET_EXAMPLES_UI_SNAPSHOT_VIEW_H_

#include <lib/async-loop/cpp/loop.h>

#include <fuchsia/scenic/snapshot/cpp/fidl.h>

#include "garnet/lib/ui/gfx/resources/snapshot/snapshot_generated.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/ui/view_framework/base_view.h"

namespace snapshot {

// A view that displays saved snapshot of views.
class View final : public mozart::BaseView,
                   public fuchsia::scenic::snapshot::Loader {
 public:
  View(async::Loop* loop, component::StartupContext* startup_context,
       ::fuchsia::ui::viewsv1::ViewManagerPtr view_manager,
       fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
           view_owner_request,
       fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> outgoing_services);

  ~View() override = default;

  // |fuchsia::scenic::snapshot::Loader|.
  virtual void Load(::fuchsia::mem::Buffer payload) override;

 private:
  fidl::BindingSet<fuchsia::scenic::snapshot::Loader> loader_bindings_;
  component::ServiceNamespace service_namespace_;

  void LoadNode(scenic::ContainerNode& parent_node,
                const snapshot::Node* flat_node);
  void LoadShape(scenic::EntityNode& parent_node,
                 const snapshot::Node* flat_node);
  void LoadMaterial(scenic::ShapeNode& parent_node,
                    const snapshot::Node* flat_node);

  FXL_DISALLOW_COPY_AND_ASSIGN(View);
};

}  // namespace snapshot

#endif  // GARNET_EXAMPLES_UI_SNAPSHOT_VIEW_H_
