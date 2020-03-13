// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>
#include <utility>

#include "src/modular/lib/app_driver/cpp/app_driver.h"
#include "src/modular/lib/fidl/single_service_app.h"
#include "src/modular/lib/fidl/view_host.h"

namespace {

// Implementation of a minimal session shell used for testing purposes.
class TestSessionShellApp : public modular::ViewApp,
                            public fuchsia::modular::StoryProviderWatcher,
                            public fuchsia::modular::SessionShell {
 public:
  explicit TestSessionShellApp(sys::ComponentContext* const component_context)
      : ViewApp(component_context), story_provider_watcher_(this) {
    component_context->outgoing()->AddPublicService(session_shell_bindings_.GetHandler(this));

    component_context->svc()->Connect(session_shell_context_.NewRequest());
    session_shell_context_->GetStoryProvider(story_provider_.NewRequest());
    story_provider_->GetStories2(story_provider_watcher_.NewBinding(),
                                 [](std::vector<fuchsia::modular::StoryInfo2>) {});

    component_context_ = sys::ComponentContext::Create();
  }

  virtual ~TestSessionShellApp() override = default;

  // move-only
  TestSessionShellApp(const TestSessionShellApp&) = delete;
  void operator=(const TestSessionShellApp&) = delete;

 private:
  // |ViewApp|
  void CreateView(
      zx::eventpair view_event_pair,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> /*incoming_services*/,
      fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> /*outgoing_services*/) override {
    fuchsia::ui::views::ViewToken view_token;
    view_token.value = std::move(view_event_pair);
    auto scenic = component_context()->svc()->Connect<fuchsia::ui::scenic::Scenic>();
    scenic::ViewContext context = {
        .session_and_listener_request =
            scenic::CreateScenicSessionPtrAndListenerRequest(scenic.get()),
        .view_token = std::move(view_token),
        .component_context = component_context_.get(),
    };
    view_ = std::make_unique<modular::ViewHost>(std::move(context));
  }

  // |fuchsia::modular::StoryProviderWatcher|
  void OnChange2(fuchsia::modular::StoryInfo2 story_info, fuchsia::modular::StoryState story_state,
                 fuchsia::modular::StoryVisibilityState story_visibility_state) override {}

  // |fuchsia::modular::StoryProviderWatcher|
  void OnDelete(std::string story_id) override {}

  // |SessionShell|
  void AttachView(fuchsia::modular::ViewIdentifier view_id,
                  fuchsia::ui::views::ViewHolderToken view_holder_token) override {
    view_->ConnectView(std::move(view_holder_token));
  }

  // |SessionShell|
  void AttachView2(fuchsia::modular::ViewIdentifier view_id,
                   fuchsia::ui::views::ViewHolderToken view_holder_token) override {
    AttachView(view_id, std::move(view_holder_token));
  }

  // |SessionShell|
  void DetachView(fuchsia::modular::ViewIdentifier view_id, fit::function<void()> done) override {
    done();
  }

  fuchsia::modular::SessionShellContextPtr session_shell_context_;
  fidl::BindingSet<fuchsia::modular::SessionShell> session_shell_bindings_;
  fuchsia::modular::StoryProviderPtr story_provider_;

  std::unique_ptr<modular::ViewHost> view_;
  fidl::Binding<StoryProviderWatcher> story_provider_watcher_;

  std::unique_ptr<sys::ComponentContext> component_context_;
};

}  // namespace

int main(int argc, const char** argv) {
  syslog::InitLogger({"test_session_shell"});
  
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto context = sys::ComponentContext::Create();
  modular::AppDriver<TestSessionShellApp> driver(
      context->outgoing(), std::make_unique<TestSessionShellApp>(context.get()),
      [&loop] { loop.Quit(); });

  loop.Run();
  return 0;
}
