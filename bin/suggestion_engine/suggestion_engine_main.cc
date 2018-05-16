// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <modular/cpp/fidl.h>

#include "lib/app/cpp/application_context.h"
#include "lib/app_driver/cpp/app_driver.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "peridot/bin/suggestion_engine/debug.h"
#include "peridot/bin/suggestion_engine/suggestion_engine_impl.h"

namespace modular {

class SuggestionEngineApp {
 public:
  SuggestionEngineApp(component::ApplicationContext* const context) {
    context->ConnectToEnvironmentService(intelligence_services_.NewRequest());

    media::AudioServerPtr audio_server;
    context->ConnectToEnvironmentService(audio_server.NewRequest());

    engine_impl_ = std::make_unique<modular::SuggestionEngineImpl>(
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

  fxl::WeakPtr<SuggestionDebugImpl> debug() {
    return engine_impl_->debug();
  }

 private:
  std::unique_ptr<SuggestionEngineImpl> engine_impl_;
  IntelligenceServicesPtr intelligence_services_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SuggestionEngineApp);
};


}  // namespace modular

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;
  auto context = component::ApplicationContext::CreateFromStartupInfo();
  auto suggestion_engine = std::make_unique<modular::SuggestionEngineApp>(
      context.get());

  fxl::WeakPtr<modular::SuggestionDebugImpl> debug = suggestion_engine->debug();
  debug->GetIdleWaiter()->SetMessageLoop(&loop);

  modular::AppDriver<modular::SuggestionEngineApp> driver(
      context->outgoing().deprecated_services(),
      std::move(suggestion_engine),
      [&loop] { loop.QuitNow(); });

  // The |WaitUntilIdle| debug functionality escapes the main message loop to
  // perform its test.
  do {
    loop.Run();
  } while (debug && debug->GetIdleWaiter()->FinishIdleCheck());

  return 0;
}
