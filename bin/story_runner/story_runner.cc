// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of the story runner application and of all services
// that it provides directly or transitively through other services.

#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/lib/fidl/strong_binding.h"
#include "apps/modular/services/application/application_launcher.fidl.h"
#include "apps/modular/services/story/story_runner.fidl.h"
#include "apps/ledger/services/ledger.fidl.h"
#include "apps/modular/src/story_runner/story_impl.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {

// The StoryRunnerApp provides the StoryRunner service. The story
// factory doesn't need separate instances because it doesn't have
// state or parameters, so the StoryRunnerApp implements it directly.
class StoryRunnerApp : public StoryRunnerFactory {
 public:
  StoryRunnerApp()
      : application_context_(ApplicationContext::CreateFromStartupInfo()) {
    application_context_->outgoing_services()->AddService<StoryRunnerFactory>(
        [this](fidl::InterfaceRequest<StoryRunnerFactory> request) {
          bindings_.AddBinding(this, std::move(request));
        });
  }

 private:
  // |StoryRunnerFactory|
  void Create(
      fidl::InterfaceHandle<Resolver> resolver,
      fidl::InterfaceHandle<StoryStorage> story_storage,
      fidl::InterfaceHandle<ledger::LedgerRepository> user_ledger_repo,
      fidl::InterfaceRequest<StoryRunner> story_runner_request) override {
    new StoryImpl(application_context_,
                  std::move(resolver),
                  std::move(story_storage),
                  std::move(user_ledger_repo),
                  std::move(story_runner_request));
  }

  fidl::BindingSet<StoryRunnerFactory> bindings_;
  std::shared_ptr<ApplicationContext> application_context_;
  FTL_DISALLOW_COPY_AND_ASSIGN(StoryRunnerApp);
};

}  // namespace modular

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  modular::StoryRunnerApp app;
  loop.Run();
  return 0;
}
