
// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of the fuchsia::modular::StoryShell service that just lays out
// the views of all modules side by side.

#include <memory>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/app_driver/cpp/app_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>

#include "peridot/lib/fidl/single_service_app.h"
#include "peridot/lib/fidl/view_host.h"

namespace {

class DevStoryShellApp
    : public modular::SingleServiceApp<fuchsia::modular::StoryShell> {
 public:
  DevStoryShellApp(component::StartupContext* const startup_context)
      : SingleServiceApp(startup_context) {}

  ~DevStoryShellApp() override = default;

 private:
  // |SingleServiceApp|
  void CreateView(
      fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
          view_owner_request,
      fidl::InterfaceRequest<
          fuchsia::sys::ServiceProvider> /*services_request*/) override {
    view_owner_request_ = std::move(view_owner_request);
    Connect();
  }

  // |fuchsia::modular::StoryShell|
  void Initialize(fidl::InterfaceHandle<fuchsia::modular::StoryShellContext>
                      story_shell_context) override {
    story_shell_context_.Bind(std::move(story_shell_context));
    Connect();
  }

  // |fuchsia::modular::StoryShell|
  void AddSurface(
      fuchsia::modular::ViewConnection view_connection,
      fuchsia::modular::SurfaceInfo /*surface_info*/) override {
    if (view_) {
      view_->ConnectView(std::move(view_connection.owner));
    } else {
      child_views_.push_back(std::move(view_connection.owner));
    }
  }

  // |fuchsia::modular::StoryShell|
  void FocusSurface(fidl::StringPtr /*surface_id*/) override {}

  // |fuchsia::modular::StoryShell|
  void DefocusSurface(fidl::StringPtr /*surface_id*/,
                   DefocusSurfaceCallback callback) override {
    callback();
  }

  // |fuchsia::modular::StoryShell|
  void AddContainer(
      fidl::StringPtr /*container_name*/, fidl::StringPtr /*parent_id*/,
      fuchsia::modular::SurfaceRelation /* relation */,
      fidl::VectorPtr<fuchsia::modular::ContainerLayout> /*layout*/,
      fidl::VectorPtr<
          fuchsia::modular::ContainerRelationEntry> /* relationships */,
      fidl::VectorPtr<fuchsia::modular::ContainerView> /* views */) override {}

  // |fuchsia::modular::StoryShell|
  void RemoveSurface(fidl::StringPtr /*surface_id*/) override {}

  // |fuchsia::modular::StoryShell|
  void ReconnectView(fuchsia::modular::ViewConnection view_connection) override {}

  // |fuchsia::modular::StoryShell|
  void UpdateSurface(fuchsia::modular::ViewConnection view_connection,
        fuchsia::modular::SurfaceInfo /*surface_info*/) override {};

  void Connect() {
    if (story_shell_context_.is_bound() && view_owner_request_) {
      view_ = std::make_unique<modular::ViewHost>(
          startup_context()
              ->ConnectToEnvironmentService<
                  fuchsia::ui::viewsv1::ViewManager>(),
          std::move(view_owner_request_));

      for (auto& view_owner : child_views_) {
        view_->ConnectView(std::move(view_owner));
      }

      child_views_.clear();
    }
  }

  std::unique_ptr<modular::ViewHost> view_;
  std::vector<fidl::InterfaceHandle<fuchsia::ui::viewsv1token::ViewOwner>>
      child_views_;

  fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
      view_owner_request_;
  fuchsia::modular::StoryShellContextPtr story_shell_context_;
  FXL_DISALLOW_COPY_AND_ASSIGN(DevStoryShellApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::AppDriver<DevStoryShellApp> driver(
      context->outgoing().deprecated_services(),
      std::make_unique<DevStoryShellApp>(context.get()),
      [&loop] { loop.Quit(); });

  loop.Run();
  return 0;
}
