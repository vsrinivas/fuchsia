// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_VIEW_MANAGER_VIEW_MANAGER_APP_H_
#define SERVICES_UI_VIEW_MANAGER_VIEW_MANAGER_APP_H_

#include <memory>

#include "apps/mozart/services/composition/interfaces/compositor.mojom.h"
#include "apps/mozart/services/views/interfaces/view_manager.mojom.h"
#include "apps/mozart/src/view_manager/view_registry.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/bindings/strong_binding_set.h"

namespace view_manager {

// View manager application entry point.
class ViewManagerApp : public mojo::ApplicationImplBase {
 public:
  ViewManagerApp();
  ~ViewManagerApp() override;

 private:
  // |ApplicationImplBase|:
  void OnInitialize() override;
  bool OnAcceptConnection(
      mojo::ServiceProviderImpl* service_provider_impl) override;

  void OnCompositorConnectionError();

  void Shutdown();

  mojo::StrongBindingSet<mojo::ui::ViewManager> view_managers_;
  std::unique_ptr<ViewRegistry> registry_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ViewManagerApp);
};

}  // namespace view_manager

#endif  // SERVICES_UI_VIEW_MANAGER_VIEW_MANAGER_APP_H_
