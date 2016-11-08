// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/mojo/single_service_application.h"
#include "apps/modular/mojo/strong_binding.h"
#include "apps/modular/services/story/resolver.fidl.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {

class ResolverImpl : public Resolver {
 public:
  explicit ResolverImpl(fidl::InterfaceRequest<Resolver> request)
      : binding_(this, std::move(request)) {}

  ~ResolverImpl() override {}

 private:
  void Resolve(const fidl::String& query,
               const ResolveCallback& callback) override {
    callback(query);
  }

  StrongBinding<Resolver> binding_;
  FTL_DISALLOW_COPY_AND_ASSIGN(ResolverImpl);
};

class ResolverFactoryImpl : public ResolverFactory {
 public:
  ResolverFactoryImpl(fidl::InterfaceRequest<ResolverFactory> request)
      : binding_(this, std::move(request)) {}

  ~ResolverFactoryImpl() override = default;

 private:
  void GetResolver(fidl::InterfaceRequest<Resolver> request) override {
    new ResolverImpl(std::move(request));
  }

  StrongBinding<ResolverFactory> binding_;
  FTL_DISALLOW_COPY_AND_ASSIGN(ResolverFactoryImpl);
};

}  // namespace modular

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  modular::SingleServiceApplication<modular::ResolverFactory,
                                    modular::ResolverFactoryImpl>
      app;
  loop.Run();
  return 0;
}
