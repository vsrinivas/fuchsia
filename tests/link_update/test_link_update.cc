// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "application/lib/app/connect.h"
#include "apps/modular/lib/fidl/single_service_view_app.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "apps/modular/lib/testing/component_base.h"
#include "apps/modular/lib/testing/reporting.h"
#include "apps/modular/lib/testing/testing.h"
#include "apps/modular/services/story/link.fidl.h"
#include "apps/modular/services/user/user_shell.fidl.h"
#include "apps/mozart/services/views/view_manager.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

// A simple link watcher implementation that is used to push the test sequence
// forward when a condition is met.
class LinkWatcherImpl : modular::LinkWatcher {
 public:
  LinkWatcherImpl() : binding_(this) {}
  ~LinkWatcherImpl() override = default;

  // Registers itself as watcher on the given link. Only one link at a time can
  // be watched.
  void Watch(modular::LinkPtr* const link) {
    (*link)->WatchAll(binding_.NewBinding());
  }

  // Sets the function where to continue after enough changes were observed on
  // the link.
  void Continue(std::function<void()> at) { continue_ = at; }

 private:
  // |LinkWatcher|
  void Notify(const fidl::String& json) override {
    FTL_LOG(INFO) << "LinkWatcher: " << json;
    if (json == "2") {
      continue_();
    }
  }

  std::function<void()> continue_;
  fidl::Binding<modular::LinkWatcher> binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LinkWatcherImpl);
};

// Tests how data are updated in a Link.
class TestApp : modular::testing::ComponentViewBase<modular::UserShell> {
 public:
  static void New() {
    new TestApp;  // will delete itself in Terminate().
  }

 private:
  TestApp() {
    TestInit(__FILE__);
  }

  ~TestApp() override = default;

  using TestPoint = modular::testing::TestPoint;

  // |SingleServiceViewApp|
  void CreateView(
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      fidl::InterfaceRequest<app::ServiceProvider> services) override {
  }

  TestPoint initialize_{"Initialize()"};
  TestPoint story_create_{"Story Create"};

  // |UserShell|
  void Initialize(fidl::InterfaceHandle<modular::UserShellContext>
                      user_shell_context) override {
    initialize_.Pass();

    user_shell_context_.Bind(std::move(user_shell_context));
    user_shell_context_->GetStoryProvider(story_provider_.NewRequest());

    story_provider_->CreateStory("file:///system/apps/modular_tests/null_module",
                                 [this](const fidl::String& story_id) {
                                   story_create_.Pass();
                                   GetController(story_id);
                                 });
  }

  void GetController(const fidl::String& story_id) {
    story_provider_->GetController(story_id, story_controller_.NewRequest());

    story_controller_->GetLink(fidl::Array<fidl::String>::New(0), "root",
                               root_link_.NewRequest());

    link_watcher_.Continue([this] { user_shell_context_->Logout(); });
    link_watcher_.Watch(&root_link_);

    // TODO(mesch): Doesn't work yet because of FW-208.
    //root_link_->Set(nullptr, "1");
    root_link_->Set(nullptr, "2");
  }

  TestPoint terminate_{"Terminate"};

  // |UserShell|
  void Terminate(const TerminateCallback& done) override {
    terminate_.Pass();
    DeleteAndQuit(done);
  }

  LinkWatcherImpl link_watcher_;

  modular::UserShellContextPtr user_shell_context_;
  modular::StoryProviderPtr story_provider_;
  modular::StoryControllerPtr story_controller_;
  modular::LinkPtr root_link_;
  modular::StoryInfoPtr story_info_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  TestApp::New();
  loop.Run();
  return 0;
}
