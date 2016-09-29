// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/view_framework/view_provider_app.h"

#include "mojo/public/cpp/application/service_provider_impl.h"

namespace mozart {

class ViewProviderApp::DelegatingViewProvider : public ViewProvider {
 public:
  DelegatingViewProvider(ViewProviderApp* app,
                         const std::string& view_provider_url)
      : app_(app), view_provider_url_(view_provider_url) {}

  ~DelegatingViewProvider() override {}

 private:
  // |ViewProvider|:
  void CreateView(
      mojo::InterfaceRequest<ViewOwner> view_owner_request,
      mojo::InterfaceRequest<mojo::ServiceProvider> services) override {
    app_->CreateView(this, view_provider_url_, view_owner_request.Pass(),
                     services.Pass());
  }

  ViewProviderApp* app_;
  std::string view_provider_url_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DelegatingViewProvider);
};

ViewProviderApp::ViewProviderApp() {}

ViewProviderApp::~ViewProviderApp() {}

bool ViewProviderApp::OnAcceptConnection(
    mojo::ServiceProviderImpl* service_provider_impl) {
  service_provider_impl->AddService<ViewProvider>(
      [this](const mojo::ConnectionContext& connection_context,
             mojo::InterfaceRequest<ViewProvider> view_provider_request) {
        bindings_.AddBinding(
            new DelegatingViewProvider(this, connection_context.connection_url),
            view_provider_request.Pass());
      });
  return true;
}

void ViewProviderApp::CreateView(
    DelegatingViewProvider* provider,
    const std::string& view_provider_url,
    mojo::InterfaceRequest<ViewOwner> view_owner_request,
    mojo::InterfaceRequest<mojo::ServiceProvider> services) {
  CreateView(view_provider_url, view_owner_request.Pass(), services.Pass());
}

}  // namespace mozart
