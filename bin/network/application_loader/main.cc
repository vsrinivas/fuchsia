// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/application_context.h"
#include "application/services/application_loader.fidl.h"
#include "apps/network/services/network_service.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

class NetworkApplicationLoader : public app::ApplicationLoader {
 public:
  NetworkApplicationLoader()
      : context_(app::ApplicationContext::CreateFromStartupInfo()) {
    context_->outgoing_services()->AddService<app::ApplicationLoader>(
        [this](fidl::InterfaceRequest<app::ApplicationLoader> request) {
          bindings_.AddBinding(this, std::move(request));
        });

    context_->ConnectToEnvironmentService(net_.NewRequest());
  }

  void LoadApplication(
      const fidl::String& url,
      const ApplicationLoader::LoadApplicationCallback& callback) override {
    network::URLLoaderPtr loader;
    net_->CreateURLLoader(loader.NewRequest());

    auto request = network::URLRequest::New();
    request->method = "GET";
    request->url = url;
    request->auto_follow_redirects = true;
    request->response_body_mode = network::URLRequest::ResponseBodyMode::BUFFER;

    loader->Start(std::move(request),
                  ftl::MakeCopyable([ loader = std::move(loader), callback ](
                      network::URLResponsePtr response) {
                    if (response->status_code == 200) {
                      auto package = app::ApplicationPackage::New();
                      package->data = std::move(response->body->get_buffer());
                      callback(std::move(package));
                    } else {
                      callback(nullptr);
                    }
                  }));
  }

 private:
  std::unique_ptr<app::ApplicationContext> context_;
  fidl::BindingSet<app::ApplicationLoader> bindings_;

  network::NetworkServicePtr net_;
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  NetworkApplicationLoader app;
  loop.Run();
  return 0;
}
