// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/ui/base_view/cpp/view_provider_component.h"

#include "src/lib/fxl/logging.h"
#include "lib/ui/scenic/cpp/view_token_pair.h"

using fuchsia::ui::views::ViewToken;

namespace scenic {

ViewProviderComponent::ViewProviderComponent(
    ViewFactory factory, async::Loop* loop,
    component::StartupContext* startup_context)
    : startup_context_(
          startup_context
              ? std::unique_ptr<component::StartupContext>(startup_context)
              : component::StartupContext::CreateFromStartupInfo()),
      scenic_(startup_context_
                  ->ConnectToEnvironmentService<fuchsia::ui::scenic::Scenic>()),
      service_(startup_context_.get(), scenic_.get(), factory.share()) {
  // Register the |View| service.
  startup_context_->outgoing().AddPublicService<fuchsia::ui::views::View>(
      [this, startup_context = startup_context_.get(),
       factory = std::move(factory)](
          fidl::InterfaceRequest<fuchsia::ui::views::View> request) mutable {
        view_impl_ =
            std::make_unique<ViewImpl>(factory.share(), std::move(request),
                                       scenic_.get(), startup_context);
        view_impl_->SetErrorHandler([this] { view_impl_ = nullptr; });
      });

  if (startup_context) {
    // We are only responsible for cleaning up the context if we created it
    // ourselves.  In this case we are "borrowing" an existing context that was
    // provided to us, so we shouldn't retain a unique_ptr to it.
    startup_context_.release();
  }

  scenic_.set_error_handler([loop](zx_status_t status) {
    FXL_LOG(INFO) << "Lost connection to Scenic.";
    loop->Quit();
  });
}

ViewProviderComponent::ViewImpl::ViewImpl(
    ViewFactory factory, fidl::InterfaceRequest<View> view_request,
    fuchsia::ui::scenic::Scenic* scenic,
    component::StartupContext* startup_context)
    : factory_(std::move(factory)),
      scenic_(scenic),
      startup_context_(startup_context),
      binding_(this, std::move(view_request)) {}

void ViewProviderComponent::ViewImpl::SetConfig(
    fuchsia::ui::views::ViewConfig view_config) {
  if (!view_) {
    FXL_LOG(ERROR) << "Tried to call SetConfig() before creating a view";
    OnError();
    return;
  }
  view_->SetConfig(std::move(view_config));
}

[[deprecated("SCN-1343: ViewConfig is going away")]]
void ViewProviderComponent::ViewImpl::Present(
    fuchsia::ui::views::ViewToken view_token,
    fuchsia::ui::views::ViewConfig initial_config) {
  if (view_) {
    // This should only be called once.
    FXL_LOG(ERROR) << "Present() can only be called once";
    OnError();
    return;
  }

  ViewContext context = {
      .session_and_listener_request =
          CreateScenicSessionPtrAndListenerRequest(scenic_),
      .view_token2 = std::move(view_token),
      .incoming_services = {},
      .outgoing_services = {},
      .startup_context = startup_context_,
  };
  view_ = factory_(std::move(context));
  view_->SetConfig(std::move(initial_config));
}

void ViewProviderComponent::ViewImpl::Present2(
    fuchsia::ui::views::ViewToken view_token) {
  if (view_) {
    // This should only be called once.
    FXL_LOG(ERROR) << "Present() can only be called once";
    OnError();
    return;
  }

  ViewContext context = {
      .session_and_listener_request =
      CreateScenicSessionPtrAndListenerRequest(scenic_),
      .view_token2 = std::move(view_token),
      .incoming_services = {},
      .outgoing_services = {},
      .startup_context = startup_context_,
  };
  view_ = factory_(std::move(context));
}

void ViewProviderComponent::ViewImpl::SetErrorHandler(
    fit::closure error_handler) {
  error_handler_ = std::move(error_handler);
}

void ViewProviderComponent::ViewImpl::OnError(zx_status_t epitaph_value) {
  binding_.Close(epitaph_value);
  error_handler_();
}

}  // namespace scenic
