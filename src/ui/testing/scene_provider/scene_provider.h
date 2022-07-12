// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTING_SCENE_PROVIDER_SCENE_PROVIDER_H_
#define SRC_UI_TESTING_SCENE_PROVIDER_SCENE_PROVIDER_H_

#include <fuchsia/ui/test/scene/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>

namespace ui_testing {

class SceneProvider : public fuchsia::ui::test::scene::Provider {
 public:
  explicit SceneProvider(sys::ComponentContext* context) : context_(context) {}
  ~SceneProvider() override = default;

  // |fuchsia::ui::test::scene::Provider|
  void AttachClientView(fuchsia::ui::test::scene::ProviderAttachClientViewRequest request,
                        AttachClientViewCallback callback) override;

  // |fuchsia::ui::test::scene::Provider|
  void RegisterGeometryObserver(
      fidl::InterfaceRequest<fuchsia::ui::observation::geometry::Provider> geometry_observer,
      RegisterGeometryObserverCallback callback) override;

  // Returns an interface request handler bound to this object.
  fidl::InterfaceRequestHandler<fuchsia::ui::test::scene::Provider> GetHandler();

 private:
  fidl::BindingSet<fuchsia::ui::test::scene::Provider> scene_provider_bindings_;
  sys::ComponentContext* context_ = nullptr;
};

}  // namespace ui_testing

#endif  // SRC_UI_TESTING_SCENE_PROVIDER_SCENE_PROVIDER_H_
