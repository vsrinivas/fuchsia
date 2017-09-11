// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/services/resolver/resolver.fidl.h"
#include "apps/maxwell/src/resolver/resolver_impl.h"

#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/connect.h"
#include "apps/modular/services/component/component.fidl.h"

#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

using namespace resolver;

class ResolverApp {
 public:
  ResolverApp() : context_(app::ApplicationContext::CreateFromStartupInfo()) {
    // TODO(azani): Switch to using environment services.
    auto launch_info = app::ApplicationLaunchInfo::New();
    app::ServiceProviderPtr child_services;
    component::ComponentIndexPtr component_index;

    launch_info->url = "component_manager";
    launch_info->services = child_services.NewRequest();

    context_->launcher()->CreateApplication(
        std::move(launch_info), component_index_controller_.NewRequest());

    app::ConnectToService(child_services.get(),
                          fidl::GetProxy(&component_index));

    std::unique_ptr<ResolverImpl> resolver_impl(
        new ResolverImpl(std::move(component_index)));
    resolver_impl_.swap(resolver_impl);

    // Singleton service
    context_->outgoing_services()->AddService<Resolver>(
        [this](fidl::InterfaceRequest<Resolver> request) {
          resolver_bindings_.AddBinding(resolver_impl_.get(),
                                        std::move(request));
        });
  }

 private:
  std::unique_ptr<app::ApplicationContext> context_;
  std::unique_ptr<ResolverImpl> resolver_impl_;
  app::ApplicationControllerPtr component_index_controller_;
  fidl::BindingSet<Resolver> resolver_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ResolverApp);
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  ResolverApp app;
  loop.Run();
  return 0;
}
