// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/app_driver/cpp/app_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding_set.h>

#include "peridot/bin/context_engine/context_engine_impl.h"

namespace modular {

class ContextEngineApp {
 public:
  ContextEngineApp(component::StartupContext* context) {
    auto component_context =
        context
            ->ConnectToEnvironmentService<fuchsia::modular::ComponentContext>();
    component_context->GetEntityResolver(entity_resolver_.NewRequest());
    context_engine_impl_.reset(new ContextEngineImpl(entity_resolver_.get()));

    context->outgoing().AddPublicService<fuchsia::modular::ContextEngine>(
        [this](
            fidl::InterfaceRequest<fuchsia::modular::ContextEngine> request) {
          context_engine_impl_->AddBinding(std::move(request));
        });
  }

  void Terminate(std::function<void()> done) { done(); }

  fxl::WeakPtr<ContextDebugImpl> debug() {
    return context_engine_impl_->debug();
  }

 private:
  fuchsia::modular::EntityResolverPtr entity_resolver_;
  std::unique_ptr<ContextEngineImpl> context_engine_impl_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ContextEngineApp);
};

}  // namespace modular

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  auto context = component::StartupContext::CreateFromStartupInfo();
  auto context_engine_app =
      std::make_unique<modular::ContextEngineApp>(context.get());
  fxl::WeakPtr<modular::ContextDebugImpl> debug = context_engine_app->debug();
  debug->GetIdleWaiter()->SetLoop(&loop);

  modular::AppDriver<modular::ContextEngineApp> driver(
      context->outgoing().deprecated_services(), std::move(context_engine_app),
      [&loop] { loop.Quit(); });

  // The |WaitUntilIdle| debug functionality escapes the main message loop to
  // perform its test.
  do {
    loop.Run();
    loop.ResetQuit();
  } while (debug && debug->GetIdleWaiter()->FinishIdleCheck());

  return 0;
}
