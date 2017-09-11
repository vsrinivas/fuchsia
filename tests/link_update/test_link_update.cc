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
#include "lib/ftl/command_line.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/tasks/message_loop.h"

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
    FTL_LOG(INFO) << "LinkWatcher: " << json;
    continue_(json);
  }

  std::function<void(const fidl::String&)> continue_;
  fidl::Binding<modular::LinkWatcher> binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LinkWatcherImpl);
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

  // Only update 4 is guaranteed to be delivered. It may happen that 3 is
  // stomped on by 4 in the ledger before the remote notification is
  // delivered. If 3 is delivered at all, then it's before 4.
  TestPoint notify_4_{"Notify() 4"};

  void PeerSet() {
    link_watcher_.Continue([this](const fidl::String& json) {
      if (json == "4") {
        notify_4_.Pass();
        ConcurrentSet();
      }
    });

    // Watch the log to see what values actually arrive.
    root_peer_->Set(nullptr, "3");
    root_peer_->Set(nullptr, "4");
  }

  // The local update 6 is the only one currently guaranteed to be seen
  // locally. The remote update 5 may be ignored if it arrives while a local
  // update is in progress.
  //
  // TODO(mesch): Concurrently arriving updates we want to properly reconcile,
  // rather than just let them stomp on each other. (In the case of conflicting
  // scalar data, the reconciliation will most of the time just be to pick one,
  // as it is now, but it doesn't have to be.)
  //
  // NOTE(mesch): There is no ordering guarantee between the two updates. This
  // is as intended as far as production behavior is concerned. For testing, we
  // would like to be able to force an ordering, or a conflict, but right now we
  // cannot.
  TestPoint notify_6_{"Notify() 6"};

  void ConcurrentSet() {
    link_watcher_.Continue([this](const fidl::String& json) {
      if (json == "6") {
        notify_6_.Pass();
      }
    });

    // Watch the log to see what values actually arrive, and in which order.
    root_peer_->Set(nullptr, "5");
    root_link_->Set(nullptr, "6");

    // We log out after Link updates are written to ledger. The local one is
    // guaranteed to be delivered locally by then.
    root_peer_->Sync([this] { root_link_->Sync([this] { Logout(); }); });
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

  FTL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  mtl::MessageLoop loop;
  TestApp::New();
  loop.Run();
  return 0;
}
