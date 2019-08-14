// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_BASE_VIEW_CPP_VIEW_PROVIDER_SERVICE_TRANSITIONAL_H_
#define LIB_UI_BASE_VIEW_CPP_VIEW_PROVIDER_SERVICE_TRANSITIONAL_H_

#include <fuchsia/ui/app/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include <vector>

#include "lib/fidl/cpp/binding_set.h"
#include "lib/fit/function.h"
#include "lib/ui/base_view/cpp/base_view_transitional.h"

namespace scenic {

// A callback to create a view when given a context.
using ViewFactoryTransitional =
    fit::function<std::unique_ptr<BaseViewTransitional>(ViewContextTransitional context)>;

// Publishes a view provider as an outgoing service of the application.
// The views created by the view provider are owned by it and will be destroyed
// when the view provider itself is destroyed.
//
// This is only intended to be used for simple example programs.
class ViewProviderServiceTransitional : private fuchsia::ui::app::ViewProvider {
 public:
  ViewProviderServiceTransitional(sys::ComponentContext* component_context,
                                  fuchsia::ui::scenic::Scenic* scenic, ViewFactoryTransitional factory);

  ~ViewProviderServiceTransitional();

  // |fuchsia::ui::app::ViewProvider|
  void CreateView(zx::eventpair view_token,
                  fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
                  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services) override;

 private:
  ViewProviderServiceTransitional(sys::ComponentContext* component_context,
                                  fuchsia::ui::scenic::Scenic* scenic);
  ViewProviderServiceTransitional(const ViewProviderServiceTransitional&) = delete;
  ViewProviderServiceTransitional& operator=(const ViewProviderServiceTransitional&) = delete;

  sys::ComponentContext* const component_context_;
  fuchsia::ui::scenic::Scenic* const scenic_;

  ViewFactoryTransitional view_factory_ = nullptr;
  std::vector<std::unique_ptr<BaseViewTransitional>> views_;
  fidl::BindingSet<fuchsia::ui::app::ViewProvider> bindings_;
};

}  // namespace scenic

#endif  // LIB_UI_BASE_VIEW_CPP_VIEW_PROVIDER_SERVICE_TRANSITIONAL_H_
