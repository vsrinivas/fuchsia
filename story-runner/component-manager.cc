// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include "apps/modular/mojom_hack/story_runner.mojom.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/strong_binding.h"

namespace {

using mojo::ApplicationImplBase;
using mojo::ConnectionContext;
using mojo::InterfaceRequest;
using mojo::ServiceProviderImpl;
using mojo::StrongBinding;

using story::Link;
using story::Resolver;

class ResolverImpl : public Resolver {
 public:
  explicit ResolverImpl(InterfaceRequest<Resolver> req) :
      binding_(this, req.Pass()) {}

  ~ResolverImpl() override {}

 private:
  void Resolve(const mojo::String& query,
               const ResolveCallback& callback) override {
    callback.Run(query);
  }

  mojo::StrongBinding<Resolver> binding_;
  FTL_DISALLOW_COPY_AND_ASSIGN(ResolverImpl);
};

class ComponentManagerApp : public mojo::ApplicationImplBase {
 public:
  ComponentManagerApp() {}
  ~ComponentManagerApp() override {}

 private:
  void OnInitialize() override { FTL_LOG(INFO) << "component-manager init"; }

  bool OnAcceptConnection(
      mojo::ServiceProviderImpl* service_provider_impl) override {
    // Register |ComponentManager| implementation.
    service_provider_impl->AddService<Resolver>(
        [](const mojo::ConnectionContext& connection_context,
           mojo::InterfaceRequest<Resolver> request) {
          new ResolverImpl(request.Pass());
        });
    return true;
  }

  FTL_DISALLOW_COPY_AND_ASSIGN(ComponentManagerApp);
};

}  // namespace

MojoResult MojoMain(MojoHandle request) {
  FTL_LOG(INFO) << "component-manager main";
  ComponentManagerApp app;
  return mojo::RunApplication(request, &app);
}
