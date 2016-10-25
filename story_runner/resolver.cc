// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include "apps/modular/mojo/single_service_application.h"
#include "apps/modular/story_runner/resolver.mojom.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/public/interfaces/application/application_connector.mojom.h"

namespace modular {

using mojo::ApplicationConnector;
using mojo::InterfaceHandle;
using mojo::InterfacePtr;
using mojo::InterfaceRequest;
using mojo::StrongBinding;
using mojo::String;

class ResolverImpl : public Resolver {
 public:
  explicit ResolverImpl(InterfaceRequest<Resolver> request)
      : binding_(this, std::move(request)) {}

  ~ResolverImpl() override {}

 private:
  void Resolve(const String& query, const ResolveCallback& callback) override {
    callback.Run(query);
  }

  StrongBinding<Resolver> binding_;
  FTL_DISALLOW_COPY_AND_ASSIGN(ResolverImpl);
};

class ResolverFactoryImpl : public ResolverFactory {
 public:
  ResolverFactoryImpl(InterfaceHandle<ApplicationConnector> app_connector,
                      InterfaceRequest<ResolverFactory> request)
      : binding_(this, std::move(request)) {}

  ~ResolverFactoryImpl() override {}

 private:
  void GetResolver(InterfaceRequest<Resolver> request) override {
    new ResolverImpl(std::move(request));
  }

  StrongBinding<ResolverFactory> binding_;
  FTL_DISALLOW_COPY_AND_ASSIGN(ResolverFactoryImpl);
};

}  // namespace modular

MojoResult MojoMain(MojoHandle request) {
  FTL_LOG(INFO) << "component-manager main";
  modular::SingleServiceApplication<modular::ResolverFactory,
                                    modular::ResolverFactoryImpl>
      app;
  return mojo::RunApplication(request, &app);
}
