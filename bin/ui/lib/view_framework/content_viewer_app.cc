// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/view_framework/content_viewer_app.h"

#include "apps/mozart/lib/view_framework/view_provider_app.h"
#include "mojo/public/cpp/application/service_provider_impl.h"

namespace mojo {
namespace ui {

class ContentViewerApp::DelegatingContentHandler : public mojo::ContentHandler {
 public:
  DelegatingContentHandler(ContentViewerApp* app,
                           const std::string& content_handler_url)
      : app_(app), content_handler_url_(content_handler_url) {}

  ~DelegatingContentHandler() override {}

 private:
  // |ContentHandler|:
  void StartApplication(
      mojo::InterfaceRequest<mojo::Application> application_request,
      mojo::URLResponsePtr response) override {
    app_->StartViewer(content_handler_url_, application_request.Pass(),
                      response.Pass());
  }

  ContentViewerApp* app_;
  std::string content_handler_url_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DelegatingContentHandler);
};

ContentViewerApp::ContentViewerApp() {}

ContentViewerApp::~ContentViewerApp() {}

bool ContentViewerApp::OnAcceptConnection(
    ServiceProviderImpl* service_provider_impl) {
  service_provider_impl->AddService<ContentHandler>([this](
      const ConnectionContext& connection_context,
      InterfaceRequest<ContentHandler> content_handler_request) {
    bindings_.AddBinding(
        new DelegatingContentHandler(this, connection_context.connection_url),
        content_handler_request.Pass());
  });
  return true;
}

void ContentViewerApp::StartViewer(
    const std::string& content_handler_url,
    mojo::InterfaceRequest<mojo::Application> application_request,
    mojo::URLResponsePtr response) {
  // TODO(vtl): This is usually leaky, since |*app| (the returned
  // |ApplicationImplBase|/|ViewProviderApp| implementation) typically doesn't
  // own itself. Probably |LoadContent()| should take the |application_request|,
  // and not return anything. This method doesn't really appear to add anything.
  ViewProviderApp* app = LoadContent(content_handler_url, response.Pass());
  if (app)
    app->Bind(application_request.Pass());
}

}  // namespace ui
}  // namespace mojo
