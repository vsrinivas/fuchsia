// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/app_driver/cpp/app_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include "peridot/lib/fidl/single_service_app.h"
#include "peridot/lib/fidl/view_host.h"

namespace {

// Implementation of the Story Shell that just collects all surfaces and holds
// them.
class TestStoryShellApp
    : public modular::SingleServiceApp<fuchsia::modular::StoryShell> {
 public:
  TestStoryShellApp(component::StartupContext* const startup_context)
      : SingleServiceApp(startup_context) {}

  ~TestStoryShellApp() override = default;

  // move-only.
  TestStoryShellApp(const TestStoryShellApp&) = delete;
  void operator=(const TestStoryShellApp&) = delete;

 private:
  // |SingleServiceApp|
  void CreateView(
      zx::eventpair view_token,
      fidl::InterfaceRequest<
          fuchsia::sys::ServiceProvider> /*incoming_services*/,
      fidl::InterfaceHandle<
          fuchsia::sys::ServiceProvider> /*outgoing_services*/) override {
    view_token_.value = std::move(view_token);
    Connect();
  }

  // |fuchsia::modular::StoryShell|
  void Initialize(fidl::InterfaceHandle<fuchsia::modular::StoryShellContext>
                      story_shell_context) override {
    story_shell_context_.Bind(std::move(story_shell_context));
    Connect();
  }

  // |fuchsia::modular::StoryShell|
  void AddSurface(fuchsia::modular::ViewConnection view_connection,
                  fuchsia::modular::SurfaceInfo /*surface_info*/) override {
    auto view_holder_token = scenic::ToViewHolderToken(
        zx::eventpair(view_connection.owner.TakeChannel().release()));

    if (view_) {
      view_->ConnectView(std::move(view_holder_token));
    } else {
      child_view_holder_tokens_.push_back(std::move(view_holder_token));
    }
  }

  // |fuchsia::modular::StoryShell|
  void FocusSurface(std::string /*surface_id*/) override {}

  // |fuchsia::modular::StoryShell|
  void DefocusSurface(std::string /*surface_id*/,
                      DefocusSurfaceCallback callback) override {
    callback();
  }

  // |fuchsia::modular::StoryShell|
  void AddContainer(
      std::string /*container_name*/, fidl::StringPtr /*parent_id*/,
      fuchsia::modular::SurfaceRelation /* relation */,
      std::vector<fuchsia::modular::ContainerLayout> /*layout*/,
      std::vector<fuchsia::modular::ContainerRelationEntry> /* relationships */,
      std::vector<fuchsia::modular::ContainerView> /* views */) override {}

  // |fuchsia::modular::StoryShell|
  void RemoveSurface(std::string /*surface_id*/) override {}

  // |fuchsia::modular::StoryShell|
  void ReconnectView(
      fuchsia::modular::ViewConnection view_connection) override {}

  // |fuchsia::modular::StoryShell|
  void UpdateSurface(fuchsia::modular::ViewConnection view_connection,
                     fuchsia::modular::SurfaceInfo /*surface_info*/) override{};

  void Connect() {
    if (story_shell_context_.is_bound() && view_token_.value) {
      auto scenic =
          startup_context()
              ->ConnectToEnvironmentService<fuchsia::ui::scenic::Scenic>();
      scenic::ViewContext view_context = {
          .session_and_listener_request =
              scenic::CreateScenicSessionPtrAndListenerRequest(scenic.get()),
          .view_token = std::move(view_token_),
          .startup_context = startup_context(),
      };

      view_ = std::make_unique<modular::ViewHost>(std::move(view_context));
      for (auto& view_holder_token : child_view_holder_tokens_) {
        view_->ConnectView(std::move(view_holder_token));
      }

      child_view_holder_tokens_.clear();
    }
  }

  std::unique_ptr<modular::ViewHost> view_;
  fuchsia::ui::views::ViewToken view_token_;
  std::vector<fuchsia::ui::views::ViewHolderToken> child_view_holder_tokens_;

  fuchsia::modular::StoryShellContextPtr story_shell_context_;
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::AppDriver<TestStoryShellApp> driver(
      context->outgoing().deprecated_services(),
      std::make_unique<TestStoryShellApp>(context.get()),
      [&loop] { loop.Quit(); });

  loop.Run();
  return 0;
}
