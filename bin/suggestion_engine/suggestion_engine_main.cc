// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/app_driver/cpp/app_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fxl/memory/weak_ptr.h>

#include "peridot/bin/suggestion_engine/debug.h"
#include "peridot/bin/suggestion_engine/suggestion_engine_impl.h"

namespace modular {

class SuggestionEngineApp {
 public:
  SuggestionEngineApp(component::StartupContext* const context) {
    context->ConnectToEnvironmentService(intelligence_services_.NewRequest());

    fuchsia::media::AudioPtr audio;
    context->ConnectToEnvironmentService(audio.NewRequest());

    engine_impl_ =
        std::make_unique<modular::SuggestionEngineImpl>(std::move(audio));

    context->outgoing().AddPublicService<fuchsia::modular::SuggestionEngine>(
        [this](fidl::InterfaceRequest<fuchsia::modular::SuggestionEngine>
                   request) { engine_impl_->Connect(std::move(request)); });
    context->outgoing().AddPublicService<fuchsia::modular::SuggestionProvider>(
        [this](fidl::InterfaceRequest<fuchsia::modular::SuggestionProvider>
                   request) { engine_impl_->Connect(std::move(request)); });
    context->outgoing().AddPublicService<fuchsia::modular::SuggestionDebug>(
        [this](
            fidl::InterfaceRequest<fuchsia::modular::SuggestionDebug> request) {
          engine_impl_->Connect(std::move(request));
        });
  }

  void Terminate(const std::function<void()>& done) { done(); }

  fxl::WeakPtr<SuggestionDebugImpl> debug() { return engine_impl_->debug(); }

 private:
  std::unique_ptr<SuggestionEngineImpl> engine_impl_;
  fuchsia::modular::IntelligenceServicesPtr intelligence_services_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SuggestionEngineApp);
};

}  // namespace modular

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = component::StartupContext::CreateFromStartupInfo();
  auto suggestion_engine =
      std::make_unique<modular::SuggestionEngineApp>(context.get());

  fxl::WeakPtr<modular::SuggestionDebugImpl> debug = suggestion_engine->debug();
  debug->GetIdleWaiter()->SetLoop(&loop);

  modular::AppDriver<modular::SuggestionEngineApp> driver(
      context->outgoing().deprecated_services(), std::move(suggestion_engine),
      [&loop] { loop.Quit(); });

  // The |WaitUntilIdle| debug functionality escapes the main message loop to
  // perform its test.
  do {
    loop.Run();
    loop.ResetQuit();
  } while (debug && debug->GetIdleWaiter()->FinishIdleCheck());

  return 0;
}
