// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_VIEW_TREE_SCOPED_OBSERVER_REGISTRY_H_
#define SRC_UI_SCENIC_LIB_VIEW_TREE_SCOPED_OBSERVER_REGISTRY_H_

#include <fuchsia/ui/observation/scope/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include "src/ui/scenic/lib/view_tree/geometry_provider.h"

namespace view_tree {

// The Registry class allows a client to receive scoped view geometry updates, in conjunction with
// the |fuchsia::ui::observation::Geometry| protocol.
class ScopedRegistry : public fuchsia::ui::observation::scope::Registry {
 public:
  // Sets up forwarding of geometry requests to the geometry provider manager.
  explicit ScopedRegistry(std::shared_ptr<view_tree::GeometryProvider> geometry_provider);

  // |fuchsia.ui.observation.scope.Registry.RegisterScopedViewTreeWatcher|.
  void RegisterScopedViewTreeWatcher(
      zx_koid_t context_view,
      fidl::InterfaceRequest<fuchsia::ui::observation::geometry::ViewTreeWatcher> request,
      ScopedRegistry::RegisterScopedViewTreeWatcherCallback callback) override;

  void Publish(sys::ComponentContext* app_context);

 private:
  fidl::BindingSet<fuchsia::ui::observation::scope::Registry> bindings_;

  std::shared_ptr<view_tree::GeometryProvider> geometry_provider_;
};

}  // namespace view_tree

#endif  // SRC_UI_SCENIC_LIB_VIEW_TREE_SCOPED_OBSERVER_REGISTRY_H_
