
// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of the StoryShell service that just lays out the
// views of all modules side by side.

#include <memory>

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/story/fidl/story_shell.fidl.h"
#include "peridot/lib/fidl/single_service_app.h"
#include "peridot/lib/fidl/view_host.h"

namespace {

class DevStoryShellApp : modular::SingleServiceApp<modular::StoryShell> {
 public:
  DevStoryShellApp() = default;
  ~DevStoryShellApp() override = default;

 private:
  // |SingleServiceApp|
  void CreateView(
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      fidl::InterfaceRequest<app::ServiceProvider> /*services_request*/)
      override {
    view_owner_request_ = std::move(view_owner_request);
    Connect();
  }

  // |StoryShell|
  void Initialize(fidl::InterfaceHandle<modular::StoryContext> story_context) override {
    story_context_.Bind(std::move(story_context));
    Connect();
  }

  // |StoryShell|
  void ConnectView(fidl::InterfaceHandle<mozart::ViewOwner> view_owner,
                   const fidl::String& /*view_id*/,
                   const fidl::String& /*parent_id*/,
                   modular::SurfaceRelationPtr /*surface_relation*/) override {
    if (view_) {
      view_->ConnectView(std::move(view_owner));
    } else {
      child_views_.push_back(std::move(view_owner));
    }
  }

  // |StoryShell|
  void FocusView(const fidl::String& /*view_id*/,
                 const fidl::String& /*relative_view_id*/) override {}

  // |StoryShell|
  void DefocusView(const fidl::String& /*view_id*/,
                   const DefocusViewCallback& callback) override {
    callback();
  }

  // |Lifecycle|
  void Terminate() override {
    FXL_LOG(INFO) << "StoryShell::Terminate()";
    fsl::MessageLoop::GetCurrent()->QuitNow();
  }

  void Connect() {
    if (story_context_.is_bound() && view_owner_request_) {
      view_ = std::make_unique<modular::ViewHost>(
          application_context()
              ->ConnectToEnvironmentService<mozart::ViewManager>(),
          std::move(view_owner_request_));

      for (auto& view_owner : child_views_) {
        view_->ConnectView(std::move(view_owner));
      }

      child_views_.clear();
    }
  }

  std::unique_ptr<modular::ViewHost> view_;
  std::vector<fidl::InterfaceHandle<mozart::ViewOwner>> child_views_;

  fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request_;
  modular::StoryContextPtr story_context_;
  FXL_DISALLOW_COPY_AND_ASSIGN(DevStoryShellApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  DevStoryShellApp app;
  loop.Run();
  return 0;
}
