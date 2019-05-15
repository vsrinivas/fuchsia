// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/app_driver/cpp/app_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>

#include <memory>
#include <utility>

#include "peridot/lib/fidl/single_service_app.h"
#include "peridot/lib/fidl/view_host.h"

namespace {

// Implementation of a minimal session shell used for testing purposes. This
// session shell listens for new stories and starts them if they are in a
// stopped state. Note that stopping a running story will cause it to start up
// again.
class TestSessionShellApp : public modular::ViewApp,
                            public fuchsia::modular::StoryProviderWatcher,
                            public fuchsia::modular::SessionShell {
 public:
  explicit TestSessionShellApp(component::StartupContext* const startup_context)
      : ViewApp(startup_context), story_provider_watcher_(this) {
    startup_context->outgoing().AddPublicService(
        session_shell_bindings_.GetHandler(this));

    startup_context->ConnectToEnvironmentService(
        session_shell_context_.NewRequest());
    session_shell_context_->GetStoryProvider(story_provider_.NewRequest());
    story_provider_->GetStories(
        story_provider_watcher_.NewBinding(),
        [](std::vector<fuchsia::modular::StoryInfo>) {});
  }

  virtual ~TestSessionShellApp() override = default;

  // move-only
  TestSessionShellApp(const TestSessionShellApp&) = delete;
  void operator=(const TestSessionShellApp&) = delete;

 private:
  // |ViewApp|
  void CreateView(
      zx::eventpair view_event_pair,
      fidl::InterfaceRequest<
          fuchsia::sys::ServiceProvider> /*incoming_services*/,
      fidl::InterfaceHandle<
          fuchsia::sys::ServiceProvider> /*outgoing_services*/) override {
    fuchsia::ui::views::ViewToken view_token;
    view_token.value = std::move(view_event_pair);
    auto scenic =
        startup_context()
            ->ConnectToEnvironmentService<fuchsia::ui::scenic::Scenic>();
    scenic::ViewContext context = {
        .session_and_listener_request =
            scenic::CreateScenicSessionPtrAndListenerRequest(scenic.get()),
        .view_token = std::move(view_token),
        .startup_context = startup_context(),
    };
    view_ = std::make_unique<modular::ViewHost>(std::move(context));
  }

  // |fuchsia::modular::StoryProviderWatcher|
  void OnChange(
      fuchsia::modular::StoryInfo story_info,
      fuchsia::modular::StoryState story_state,
      fuchsia::modular::StoryVisibilityState story_visibility_state) override {
    if (story_state == fuchsia::modular::StoryState::STOPPED) {
      fuchsia::modular::StoryControllerPtr story_controller;
      story_provider_->GetController(story_info.id,
                                     story_controller.NewRequest());
      story_controller->RequestStart();
    }
  }

  // |fuchsia::modular::StoryProviderWatcher|
  void OnDelete(std::string story_id) override {}

  // |SessionShell|
  void AttachView(
      fuchsia::modular::ViewIdentifier view_id,
      fuchsia::ui::views::ViewHolderToken view_holder_token) override {
    view_->ConnectView(std::move(view_holder_token));
  }

  // |SessionShell|
  void AttachView2(
      fuchsia::modular::ViewIdentifier view_id,
      fuchsia::ui::views::ViewHolderToken view_holder_token) override {
    AttachView(view_id, std::move(view_holder_token));
  }

  // |SessionShell|
  void DetachView(fuchsia::modular::ViewIdentifier view_id,
                  fit::function<void()> done) override {
    done();
  }

  fuchsia::modular::SessionShellContextPtr session_shell_context_;
  fidl::BindingSet<fuchsia::modular::SessionShell> session_shell_bindings_;
  fuchsia::modular::StoryProviderPtr story_provider_;

  std::unique_ptr<modular::ViewHost> view_;
  fidl::Binding<StoryProviderWatcher> story_provider_watcher_;
};

}  // namespace

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::AppDriver<TestSessionShellApp> driver(
      context->outgoing().deprecated_services(),
      std::make_unique<TestSessionShellApp>(context.get()),
      [&loop] { loop.Quit(); });

  loop.Run();
  return 0;
}
