// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "lib/app/cpp/application_context.h"
#include "lib/app_driver/cpp/app_driver.h"
#include <fuchsia/cpp/modular.h>
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fsl/tasks/message_loop.h"
#include "peridot/bin/context_engine/context_engine_impl.h"

namespace modular {
namespace {

class ContextEngineApp {
 public:
  ContextEngineApp(component::ApplicationContext* app_context) {
    auto component_context =
        app_context->ConnectToEnvironmentService<ComponentContext>();
    component_context->GetEntityResolver(entity_resolver_.NewRequest());
    context_engine_impl_.reset(new ContextEngineImpl(entity_resolver_.get()));

    app_context->outgoing_services()->AddService<ContextEngine>(
        [this](fidl::InterfaceRequest<ContextEngine> request) {
          context_engine_impl_->AddBinding(std::move(request));
        });
  }

  void Terminate(std::function<void()> done) { done(); }

  fxl::WeakPtr<ContextDebugImpl> debug() {
    return context_engine_impl_->debug();
  }

 private:
  EntityResolverPtr entity_resolver_;
  std::unique_ptr<ContextEngineImpl> context_engine_impl_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ContextEngineApp);
};

}  // namespace
}  // namespace modular

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;
  auto app_context = component::ApplicationContext::CreateFromStartupInfo();
  auto context_engine_app =
      std::make_unique<modular::ContextEngineApp>(app_context.get());
  fxl::WeakPtr<modular::ContextDebugImpl> debug = context_engine_app->debug();

  modular::AppDriver<modular::ContextEngineApp> driver(
      app_context->outgoing_services(), std::move(context_engine_app),
      [&loop] { loop.QuitNow(); });

  // The |WaitUntilIdle| debug functionality escapes the main message loop to
  // perform its test.
  do {
    loop.Run();
  } while (debug && debug->FinishIdleCheck());

  return 0;
}
