// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/services/story/resolver.fidl.h"
#include "apps/modular/lib/app/application_context.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {

// Because the Resolver service doesn't have state or takes
// parameters, the resolver app doesn't even create instances of it.
// It only ever returns bindings to its sole instance.
class ResolverApp : public Resolver {
 public:
  ResolverApp() :
      application_context_(ApplicationContext::CreateFromStartupInfo()) {
    application_context_->outgoing_services()->AddService<Resolver>(
        [this](fidl::InterfaceRequest<Resolver> request) {
          bindings_.AddBinding(this, std::move(request));
        });
  }

 private:
  void Resolve(const fidl::String& query,
               const ResolveCallback& callback) override {
    callback(query);
  }

  fidl::BindingSet<Resolver> bindings_;
  std::unique_ptr<ApplicationContext> application_context_;
  FTL_DISALLOW_COPY_AND_ASSIGN(ResolverApp);
};

}  // namespace modular

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  modular::ResolverApp app;
  loop.Run();
  return 0;
}
