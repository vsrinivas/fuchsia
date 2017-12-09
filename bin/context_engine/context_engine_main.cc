// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "lib/app/cpp/application_context.h"
#include "lib/app_driver/cpp/app_driver.h"
#include "lib/component/fidl/component_context.fidl.h"
#include "lib/entity/fidl/entity_resolver.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fsl/tasks/message_loop.h"
#include "peridot/bin/context_engine/context_engine_impl.h"

namespace maxwell {
namespace {

class ContextEngineApp {
 public:
  ContextEngineApp(app::ApplicationContext* app_context) {
    auto component_context =
        app_context->ConnectToEnvironmentService<modular::ComponentContext>();
    component_context->GetEntityResolver(entity_resolver_.NewRequest());
    context_engine_impl_.reset(new ContextEngineImpl(entity_resolver_.get()));

    app_context->outgoing_services()->AddService<ContextEngine>(
        [this](fidl::InterfaceRequest<ContextEngine> request) {
          context_engine_impl_->AddBinding(std::move(request));
        });
  }

  void Terminate(std::function<void()> done) { done(); }

 private:
  modular::EntityResolverPtr entity_resolver_;
  std::unique_ptr<ContextEngineImpl> context_engine_impl_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ContextEngineApp);
};

}  // namespace
}  // namespace maxwell

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;
  auto context = app::ApplicationContext::CreateFromStartupInfo();
  modular::AppDriver<maxwell::ContextEngineApp> driver(
      context->outgoing_services(),
      std::make_unique<maxwell::ContextEngineApp>(context.get()),
      [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
