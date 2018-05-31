// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>

#include "lib/app/cpp/startup_context.h"
#include "lib/app_driver/cpp/app_driver.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "peridot/bin/suggestion_engine/debug.h"
#include "peridot/bin/suggestion_engine/suggestion_engine_impl.h"

namespace fuchsia {
namespace modular {

class SuggestionEngineApp {
 public:
  SuggestionEngineApp(component::StartupContext* const context) {
    context->ConnectToEnvironmentService(intelligence_services_.NewRequest());

    media::AudioServerPtr audio_server;
    context->ConnectToEnvironmentService(audio_server.NewRequest());

    engine_impl_ = std::make_unique<fuchsia::modular::SuggestionEngineImpl>(
        std::move(audio_server));

    context->outgoing().AddPublicService<SuggestionEngine>(
        [this](fidl::InterfaceRequest<SuggestionEngine> request) {
          engine_impl_->Connect(std::move(request));
        });
    context->outgoing().AddPublicService<SuggestionProvider>(
        [this](fidl::InterfaceRequest<SuggestionProvider> request) {
          engine_impl_->Connect(std::move(request));
        });
    context->outgoing().AddPublicService<SuggestionDebug>(
        [this](fidl::InterfaceRequest<SuggestionDebug> request) {
          engine_impl_->Connect(std::move(request));
        });
  }

  void Terminate(const std::function<void()>& done) { done(); }

  fxl::WeakPtr<SuggestionDebugImpl> debug() { return engine_impl_->debug(); }

 private:
  std::unique_ptr<SuggestionEngineImpl> engine_impl_;
  IntelligenceServicesPtr intelligence_services_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SuggestionEngineApp);
};

}  // namespace modular
}  // namespace fuchsia

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;
  auto context = component::StartupContext::CreateFromStartupInfo();
  auto suggestion_engine =
      std::make_unique<fuchsia::modular::SuggestionEngineApp>(context.get());

  fxl::WeakPtr<fuchsia::modular::SuggestionDebugImpl> debug =
      suggestion_engine->debug();
  debug->GetIdleWaiter()->SetMessageLoop(&loop);

  fuchsia::modular::AppDriver<fuchsia::modular::SuggestionEngineApp> driver(
      context->outgoing().deprecated_services(), std::move(suggestion_engine),
      [&loop] { loop.QuitNow(); });

  // The |WaitUntilIdle| debug functionality escapes the main message loop to
  // perform its test.
  do {
    loop.Run();
  } while (debug && debug->GetIdleWaiter()->FinishIdleCheck());

  return 0;
}
