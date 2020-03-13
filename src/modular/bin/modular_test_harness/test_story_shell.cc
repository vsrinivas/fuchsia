// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>

#include "src/modular/lib/app_driver/cpp/app_driver.h"
#include "src/modular/lib/fidl/single_service_app.h"
#include "src/modular/lib/fidl/view_host.h"

namespace {

// Implementation of the Story Shell that just collects all surfaces and holds
// them.
class TestStoryShellApp : public modular::SingleServiceApp<fuchsia::modular::StoryShell> {
 public:
  TestStoryShellApp(sys::ComponentContext* const component_context)
      : SingleServiceApp(component_context) {
    component_context_ = sys::ComponentContext::Create();
  }

  ~TestStoryShellApp() override = default;

  // move-only.
  TestStoryShellApp(const TestStoryShellApp&) = delete;
  void operator=(const TestStoryShellApp&) = delete;

 private:
  // |SingleServiceApp|
  void CreateView(
      zx::eventpair view_token,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> /*incoming_services*/,
      fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> /*outgoing_services*/) override {
    view_token_.value = std::move(view_token);

    Connect();
  }

  // |fuchsia::modular::StoryShell|
  void Initialize(
      fidl::InterfaceHandle<fuchsia::modular::StoryShellContext> story_shell_context) override {
    story_shell_context_.Bind(std::move(story_shell_context));

    Connect();
  }

  // |fuchsia::modular::StoryShell|
  void AddSurface(fuchsia::modular::ViewConnection view_connection,
                  fuchsia::modular::SurfaceInfo surface_info) override {
    fuchsia::modular::SurfaceInfo2 surface_info2;
    surface_info2.set_parent_id(surface_info.parent_id);
    if (surface_info.surface_relation) {
      surface_info2.set_surface_relation(*surface_info.surface_relation);
    }
    if (surface_info.module_manifest) {
      surface_info2.set_module_manifest(std::move(*surface_info.module_manifest));
    }
    surface_info2.set_module_source(surface_info.module_source);
    AddSurface3(std::move(view_connection), std::move(surface_info2));
  }

  // |fuchsia::modular::StoryShell|
  void AddSurface2(fuchsia::modular::ViewConnection2 view_connection,
                   fuchsia::modular::SurfaceInfo surface_info) override {
    AddSurface(
        fuchsia::modular::ViewConnection{
            .surface_id = std::move(view_connection.surface_id),
            .view_holder_token = std::move(view_connection.view_holder_token),
        },
        std::move(surface_info));
  }

  // |fuchsia::modular::StoryShell|
  void AddSurface3(fuchsia::modular::ViewConnection view_connection,
                   fuchsia::modular::SurfaceInfo2 /*surface_info*/) override {
    if (view_) {
      view_->ConnectView(std::move(view_connection.view_holder_token));
    } else {
      child_view_holder_tokens_.push_back(std::move(view_connection.view_holder_token));
    }
  }

  // |fuchsia::modular::StoryShell|
  void FocusSurface(std::string /*surface_id*/) override {}

  // |fuchsia::modular::StoryShell|
  void DefocusSurface(std::string /*surface_id*/, DefocusSurfaceCallback callback) override {
    callback();
  }

  // |fuchsia::modular::StoryShell|
  void RemoveSurface(std::string /*surface_id*/) override {}

  // |fuchsia::modular::StoryShell|
  void UpdateSurface(fuchsia::modular::ViewConnection /*view_connection*/,
                     fuchsia::modular::SurfaceInfo /*surface_info*/) override{};

  void Connect() {
    if (story_shell_context_.is_bound() && view_token_.value) {
      auto scenic = component_context()->svc()->Connect<fuchsia::ui::scenic::Scenic>();
      scenic::ViewContext view_context = {
          .session_and_listener_request =
              scenic::CreateScenicSessionPtrAndListenerRequest(scenic.get()),
          .view_token = std::move(view_token_),
          .component_context = component_context_.get(),
      };

      view_ = std::make_unique<modular::ViewHost>(std::move(view_context));
      for (auto& view_holder_token : child_view_holder_tokens_) {
        view_->ConnectView(std::move(view_holder_token));
      }

      child_view_holder_tokens_.clear();
    }
  }

  fuchsia::ui::views::ViewToken view_token_;
  std::unique_ptr<modular::ViewHost> view_;
  std::vector<fuchsia::ui::views::ViewHolderToken> child_view_holder_tokens_;

  fuchsia::modular::StoryShellContextPtr story_shell_context_;

  std::unique_ptr<sys::ComponentContext> component_context_;
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  syslog::InitLogger({"test_story_shell"});
  
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto context = sys::ComponentContext::Create();
  modular::AppDriver<TestStoryShellApp> driver(context->outgoing(),
                                               std::make_unique<TestStoryShellApp>(context.get()),
                                               [&loop] { loop.Quit(); });

  loop.Run();
  return 0;
}
