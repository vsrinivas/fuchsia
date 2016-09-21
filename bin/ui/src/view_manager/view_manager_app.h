// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_VIEW_MANAGER_VIEW_MANAGER_APP_H_
#define SERVICES_UI_VIEW_MANAGER_VIEW_MANAGER_APP_H_

#include <memory>

#include "base/macros.h"
#include "mojo/common/tracing_impl.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/bindings/strong_binding_set.h"
#include "mojo/services/gfx/composition/interfaces/compositor.mojom.h"
#include "mojo/services/ui/views/interfaces/view_manager.mojom.h"
#include "services/ui/view_manager/view_registry.h"

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

  mojo::TracingImpl tracing_;

  mojo::StrongBindingSet<mojo::ui::ViewManager> view_managers_;
  std::unique_ptr<ViewRegistry> registry_;

  DISALLOW_COPY_AND_ASSIGN(ViewManagerApp);
};

}  // namespace view_manager

#endif  // SERVICES_UI_VIEW_MANAGER_VIEW_MANAGER_APP_H_
