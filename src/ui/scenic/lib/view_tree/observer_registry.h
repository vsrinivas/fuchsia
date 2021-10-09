// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_VIEW_TREE_OBSERVER_REGISTRY_H_
#define SRC_UI_SCENIC_LIB_VIEW_TREE_OBSERVER_REGISTRY_H_

#include <fuchsia/ui/observation/test/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

namespace view_tree {

// The Registry class allows a client to receive global view geometry updates, in conjunction with
// the |fuchsia::ui::observation::Geometry| protocol.
//
// This is a sensitive protocol, so it should only be used in tests.
class Registry : public fuchsia::ui::observation::test::Registry {
  // |fuchsia.ui.observation.test.Registry|
 public:
  void RegisterGlobalGeometryProvider(
      fidl::InterfaceRequest<fuchsia::ui::observation::geometry::Provider> request,
      Registry::RegisterGlobalGeometryProviderCallback callback) override;
  void Publish(sys::ComponentContext* app_context);

 private:
  fidl::BindingSet<fuchsia::ui::observation::test::Registry> bindings_;
};

}  // namespace view_tree

#endif  // SRC_UI_SCENIC_LIB_VIEW_TREE_OBSERVER_REGISTRY_H_
