// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of the StoryShell service that just lays out the
// views of all modules side by side.

#include <memory>

#include "apps/modular/lib/fidl/single_service_view_app.h"
#include "apps/modular/lib/fidl/view_host.h"
#include "apps/modular/services/story/story_shell.fidl.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

class DummyStoryShellApp
    : modular::SingleServiceViewApp<modular::StoryShellFactory>,
      modular::StoryShell {
 public:
  DummyStoryShellApp() : story_shell_binding_(this) {}
  ~DummyStoryShellApp() override = default;

 private:
  // |SingleServiceViewApp|
  void CreateView(
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      fidl::InterfaceRequest<app::ServiceProvider> services_request) override {
    view_owner_request_ = std::move(view_owner_request);
    Connect();
  }

  // |StoryShellFactory|
  void Create(fidl::InterfaceHandle<modular::StoryContext> story_context,
              fidl::InterfaceRequest<modular::StoryShell> request) override {
    story_context_.Bind(std::move(story_context));

    FTL_DCHECK(!story_shell_binding_.is_bound());
    story_shell_binding_.Bind(std::move(request));

    Connect();
  }

  // |StoryShell|
  void ConnectView(fidl::InterfaceHandle<mozart::ViewOwner> view_owner,
                   const fidl::String& view_id,
                   const fidl::String& parent_id,
                   modular::SurfaceRelationPtr surface_relation) override {
    if (view_) {
      view_->ConnectView(std::move(view_owner));
    } else {
      child_views_.push_back(std::move(view_owner));
    }
  }

  // |StoryShell|
  void FocusView(const fidl::String& view_id,
                 const fidl::String& relative_view_id) override {}

  // |StoryShell|
  void DefocusView(const fidl::String& view_id,
                   const DefocusViewCallback& callback) override {
    callback();
  }

  // |StoryShell|
  void Terminate(const TerminateCallback& done) override {
    FTL_LOG(INFO) << "StoryShell::Terminate()";
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
    done();
  }

  void Connect() {
    if (story_context_.is_bound() && view_owner_request_) {
      view_.reset(new modular::ViewHost(
          application_context()
              ->ConnectToEnvironmentService<mozart::ViewManager>(),
          std::move(view_owner_request_)));

      for (auto& view_owner : child_views_) {
        view_->ConnectView(std::move(view_owner));
      }

      child_views_.clear();
    }
  }

  std::unique_ptr<modular::ViewHost> view_;
  std::vector<fidl::InterfaceHandle<mozart::ViewOwner>> child_views_;

  fidl::Binding<modular::StoryShell> story_shell_binding_;
  fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request_;
  modular::StoryContextPtr story_context_;
  FTL_DISALLOW_COPY_AND_ASSIGN(DummyStoryShellApp);
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  DummyStoryShellApp app;
  loop.Run();
  return 0;
}
