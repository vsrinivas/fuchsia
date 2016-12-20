// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/services/resolver/resolver.fidl-sync.h"

#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/lib/app/connect.h"
#include "apps/modular/lib/app/service_provider_impl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fidl/cpp/bindings/synchronous_interface_ptr.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

class ResolverTest {
 public:
  ResolverTest()
      : context_(modular::ApplicationContext::CreateFromStartupInfo()) {}

  void RunTests() {
    fidl::SynchronousInterfacePtr<resolver::Resolver> resolver;
    modular::ServiceProviderPtr child_services;

    auto launch_info = modular::ApplicationLaunchInfo::New();
    launch_info->url = "file:///system/apps/resolver";
    launch_info->services = child_services.NewRequest();

    context_->launcher()->CreateApplication(std::move(launch_info),
                                            resolver_controller_.NewRequest());

    modular::ConnectToService(child_services.get(),
                              fidl::GetSynchronousProxy(&resolver));

    FTL_CHECK(resolver.is_bound());
    FTL_LOG(INFO) << "Connected to " << resolver::Resolver::Name_;

    fidl::Array<resolver::ModuleInfoPtr> modules;
    FTL_CHECK(resolver->ResolveModules(
        "https://fuchsia-contracts.google.com/hello_contract", nullptr,
        &modules));
    FTL_CHECK(1 == modules.size());
    FTL_CHECK("https://www.example.com/hello" == modules[0]->component_id);
    FTL_LOG("Test passed.");
  }

 private:
  std::unique_ptr<modular::ApplicationContext> context_;
  modular::ApplicationControllerPtr resolver_controller_;
};

}  // namespace

int main(int argc, const char** argv) {
  FTL_LOG(INFO) << "resolver_test";
  mtl::MessageLoop loop;
  ResolverTest app;
  app.RunTests();
  loop.Run();
  return 0;
}
