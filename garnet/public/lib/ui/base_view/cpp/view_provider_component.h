// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_BASE_VIEW_CPP_VIEW_PROVIDER_COMPONENT_H_
#define LIB_UI_BASE_VIEW_CPP_VIEW_PROVIDER_COMPONENT_H_

#include <lib/async-loop/cpp/loop.h>
#include <memory>

#include "lib/component/cpp/startup_context.h"
#include "lib/ui/base_view/cpp/view_provider_service.h"

namespace scenic {

// Provides a skeleton for an entire component that only offers a |ViewProvider|
// and |View| service. This is only intended to be used for simple example
// programs.
//
// TODO: Rename this to |ViewComponent| and delete |ViewProviderService|
// once all |ViewProvider| implementations have been migrated to |View|.
class ViewProviderComponent {
 public:
  // Constructor for use with Views v2.
  ViewProviderComponent(ViewFactory factory, async::Loop* loop,
                        component::StartupContext* startup_context = nullptr);
  ViewProviderComponent(const ViewProviderComponent&) = delete;
  ViewProviderComponent& operator=(const ViewProviderComponent&) = delete;
  ~ViewProviderComponent() = default;

 private:
  // Implementation of the |fuchsia::ui::views::View| interface that allows it
  // to be used with |ViewProviderComponent|.
  //
  // Constructs and owns a |BaseView|.
  class ViewImpl final : public fuchsia::ui::views::View {
   public:
    // Basic constructor.
    //
    // Args:
    //   factory: Given a |ViewContext|, constructs a |BaseView|. Will only be
    //   called once.
    //   scenic: Instance of Scenic to which the |BaseView| will be attached.
    //   startup_context: Component environment.
    ViewImpl(ViewFactory factory, fidl::InterfaceRequest<View> view_request,
             fuchsia::ui::scenic::Scenic* scenic,
             component::StartupContext* startup_context);
    ~ViewImpl() override = default;

    // |fuchsia::ui::views::View|
    void Present2(fuchsia::ui::views::ViewToken view_token) override;

    // Sets the given closure as an error handler for all error types.
    void SetErrorHandler(fit::closure error_handler);

   private:
    // Performs cleanup after errors and calls the error handler, if present.
    void OnError(zx_status_t epitaph_value = ZX_ERR_BAD_STATE);

    ViewFactory factory_;
    fuchsia::ui::scenic::Scenic* scenic_;
    component::StartupContext* startup_context_;
    // |BaseView|, not to be confused with |fuchsia::ui::views::View| or
    // |scenic::View|.
    std::unique_ptr<BaseView> view_;
    fidl::Binding<View> binding_;
    fit::closure error_handler_;
  };

  std::unique_ptr<component::StartupContext> startup_context_;
  fidl::InterfacePtr<fuchsia::ui::scenic::Scenic> scenic_;
  ViewProviderService service_;
  std::unique_ptr<ViewImpl> view_impl_;
};

}  // namespace scenic

#endif  // LIB_UI_BASE_VIEW_CPP_VIEW_PROVIDER_COMPONENT_H_
