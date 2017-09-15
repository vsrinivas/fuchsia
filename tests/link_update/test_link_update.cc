// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "lib/app/cpp/connect.h"
#include "apps/modular/lib/fidl/single_service_app.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "apps/modular/lib/testing/component_base.h"
#include "apps/modular/lib/testing/reporting.h"
#include "apps/modular/lib/testing/testing.h"
#include "apps/modular/services/story/link.fidl.h"
#include "apps/modular/services/user/user_shell.fidl.h"
#include "lib/ui/views/fidl/view_manager.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/fsl/tasks/message_loop.h"

namespace {

// A simple link watcher implementation allows to specify the actual
// notification callback as a lambda and update it dynamically.
class LinkWatcherImpl : modular::LinkWatcher {
 public:
  LinkWatcherImpl() : binding_(this) {}
  ~LinkWatcherImpl() override = default;

  // Registers itself as watcher on the given link. Only one link at a time can
  // be watched.
  void Watch(modular::LinkPtr* const link) {
    (*link)->WatchAll(binding_.NewBinding());
  }

  // Sets the function that's called for a notification.
  void Continue(std::function<void(const fidl::String&)> at) { continue_ = at; }

 private:
  // |LinkWatcher|
  void Notify(const fidl::String& json) override {
    FXL_LOG(INFO) << "LinkWatcher: " << json;
    continue_(json);
  }

  std::function<void(const fidl::String&)> continue_;
  fidl::Binding<modular::LinkWatcher> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LinkWatcherImpl);
};

// Tests how data are updated in a Link.
class TestApp : modular::testing::ComponentBase<modular::UserShell> {
 public:
  static void New() {
    new TestApp;  // will delete itself in Terminate().
  }

 private:
  TestApp() { TestInit(__FILE__); }

  ~TestApp() override = default;

  using TestPoint = modular::testing::TestPoint;

  TestPoint initialize_{"Initialize()"};
  TestPoint story_create_{"Story Create"};

  // |UserShell|
  void Initialize(fidl::InterfaceHandle<modular::UserShellContext>
                      user_shell_context) override {
    initialize_.Pass();

    user_shell_context_.Bind(std::move(user_shell_context));
    user_shell_context_->GetStoryProvider(story_provider_.NewRequest());

    story_provider_->CreateStory(
        "file:///system/apps/modular_tests/null_module",
        [this](const fidl::String& story_id) {
          story_create_.Pass();
          GetController(story_id);
        });
  }

  void GetController(const fidl::String& story_id) {
    story_provider_->GetController(story_id, story_controller_.NewRequest());

    story_controller_->GetLink(nullptr, "root", root_link_.NewRequest());
    story_provider_->GetLinkPeer(story_id, nullptr, "root",
                                 root_peer_.NewRequest());

    link_watcher_.Watch(&root_link_);

    SequentialSet();
  }

  // Both updates 1 and 2 are guaranteed to be delivered, and in the order they
  // were issued.
  TestPoint notify_1_{"Notify() 1"};
  TestPoint notify_2_{"Notify() 2"};

  void SequentialSet() {
    link_watcher_.Continue([this](const fidl::String& json) {
      if (json == "1") {
        notify_1_.Pass();
      } else if (json == "2") {
        notify_2_.Pass();
        PeerSet();
      }
    });

    root_link_->Set(nullptr, "1");
    root_link_->Set(nullptr, "2");
  }

  // Only update 4 is guaranteed to be delivered on link_watcher_, although
  // if 3 is delivered at all, then it's before 4.
  TestPoint notify_4_{"Notify() 4"};

  void PeerSet() {
    link_watcher_.Continue([this](const fidl::String& json) {
      if (json == "4") {
        notify_4_.Pass();
        ConcurrentSet();
      }
    });

    // Without this nanosleep() line, 3 and 4 can have keys BEFORE 1 and 2
    // because the timestamp is at millisecond resolution with a random number
    // to break ties, which means that 3 and 4 would not overwrite the 2.
    zx_nanosleep(zx_deadline_after(ZX_MSEC(2)));

    // Watch the log to see what values are actually seen by the Watcher.
    root_peer_->Set(nullptr, "3");
    root_peer_->Set(nullptr, "4");
  }

  // The local update 6 is the only update guaranteed to be seen locally.
  // However, if update 5 is processed by the Link after update 6, it will not
  // affect the current value and so will not generate a second notification
  // for update 6.
  //
  // NOTE(mesch): There is no ordering guarantee between the two updates. This
  // is as intended as far as production behavior is concerned. For testing, we
  // would like to be able to force an ordering, or a conflict, but right now we
  // cannot.
  TestPoint notify_6_{"Notify() 6"};

  void ConcurrentSet() {
    std::shared_ptr<bool> called = std::make_shared<bool>();
    link_watcher_.Continue([this, called](const fidl::String& json) {
      if (json == "6") {
        notify_6_.Pass();
        if (!*called) {
          Logout();
          *called = true;
        }
      }
    });

    // Watch the log to see what values actually arrive, and in which order.
    root_peer_->Set(nullptr, "5");
    root_link_->Set(nullptr, "6");

    fsl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        [this, called] {
          if (!*called) {
            FXL_LOG(WARNING) << "Shutdown timed out";
            Logout();
            *called = true;
          }
        },
        fxl::TimeDelta::FromSeconds(5u));

    // The code below does not work because it does not wait for the Ledger
    // to deliver all of its messages.
    // root_link_->Sync([this] { root_peer_->Sync([this] { Logout(); }); });
  }

  void Logout() { user_shell_context_->Logout(); }

  TestPoint terminate_{"Terminate"};

  // |UserShell|
  void Terminate() override {
    terminate_.Pass();
    DeleteAndQuit();
  }

  LinkWatcherImpl link_watcher_;

  modular::UserShellContextPtr user_shell_context_;
  modular::StoryProviderPtr story_provider_;
  modular::StoryControllerPtr story_controller_;
  modular::LinkPtr root_link_;
  modular::LinkPtr root_peer_;
  modular::StoryInfoPtr story_info_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  TestApp::New();
  loop.Run();
  return 0;
}
