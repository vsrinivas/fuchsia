// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <fuchsia/modular/internal/cpp/fidl.h>
#include <lib/app/cpp/startup_context.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>

#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/component_base.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/link_update/defs.h"

using modular::testing::TestPoint;

namespace {

// A simple link watcher implementation allows to specify the actual
// notification callback as a lambda and update it dynamically.
class LinkWatcherImpl : fuchsia::modular::LinkWatcher {
 public:
  LinkWatcherImpl() : binding_(this) {}
  ~LinkWatcherImpl() override = default;

  // Registers itself as watcher on the given link. Only one link at a time can
  // be watched.
  void Watch(fuchsia::modular::LinkPtr* const link) {
    (*link)->WatchAll(binding_.NewBinding());
  }

  // Sets the function that's called for a notification.
  void Continue(std::function<void(const fidl::StringPtr&)> at) {
    continue_ = at;
  }

 private:
  // |fuchsia::modular::LinkWatcher|
  void Notify(fidl::StringPtr json) override {
    FXL_LOG(INFO) << "fuchsia::modular::LinkWatcher: " << json;
    continue_(json);
  }

  std::function<void(const fidl::StringPtr&)> continue_;
  fidl::Binding<fuchsia::modular::LinkWatcher> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LinkWatcherImpl);
};

// Cf. README.md for what this test does and how.
class TestApp
    : public modular::testing::ComponentBase<fuchsia::modular::UserShell> {
 public:
  TestApp(fuchsia::sys::StartupContext* const startup_context)
      : ComponentBase(startup_context) {
    TestInit(__FILE__);
  }

  ~TestApp() override = default;

 private:
  TestPoint initialize_{"Initialize()"};
  TestPoint story_create_{"Story Create"};

  // |fuchsia::modular::UserShell|
  void Initialize(fidl::InterfaceHandle<fuchsia::modular::UserShellContext>
                      user_shell_context) override {
    initialize_.Pass();

    user_shell_context_.Bind(std::move(user_shell_context));
    user_shell_context_->GetStoryProvider(story_provider_.NewRequest());

    story_provider_->CreateStory(kCommonNullModule,
                                 [this](const fidl::StringPtr& story_id) {
                                   story_create_.Pass();
                                   GetController(story_id);
                                 });
  }

  void GetController(const fidl::StringPtr& story_id) {
    story_provider_->GetController(story_id, story_controller_.NewRequest());

    fuchsia::modular::LinkPath link_path = fuchsia::modular::LinkPath();
    link_path.module_path = ::fidl::VectorPtr<::fidl::StringPtr>::New(0);
    link_path.link_name = "root";
    story_controller_->GetLink(std::move(link_path), root_link_.NewRequest());

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
    link_watcher_.Continue([this](const fidl::StringPtr& json) {
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
    link_watcher_.Continue([this](const fidl::StringPtr& json) {
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
  // However, if update 5 is processed by the fuchsia::modular::Link after
  // update 6, it will not affect the current value and so will not generate a
  // second notification for update 6.
  //
  // NOTE(mesch): There is no ordering guarantee between the two updates. This
  // is as intended as far as production behavior is concerned. For testing, we
  // would like to be able to force an ordering, or a conflict, but right now we
  // cannot.
  TestPoint notify_6_{"Notify() 6"};

  void ConcurrentSet() {
    std::shared_ptr<bool> called = std::make_shared<bool>();
    link_watcher_.Continue([this, called](const fidl::StringPtr& json) {
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

    async::PostDelayedTask(async_get_default(),
                           [this, called] {
                             if (!*called) {
                               FXL_LOG(WARNING) << "Shutdown timed out";
                               Logout();
                               *called = true;
                             }
                           },
                           zx::sec(5));

    // The code below does not work because it does not wait for the Ledger
    // to deliver all of its messages.
    // root_link_->Sync([this] { root_peer_->Sync([this] { Logout(); }); });
  }

  void Logout() { user_shell_context_->Logout(); }

  LinkWatcherImpl link_watcher_;

  fuchsia::modular::UserShellContextPtr user_shell_context_;
  fuchsia::modular::StoryProviderPtr story_provider_;
  fuchsia::modular::StoryControllerPtr story_controller_;
  fuchsia::modular::LinkPtr root_link_;
  fuchsia::modular::LinkPtr root_peer_;
  fuchsia::modular::StoryInfoPtr story_info_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  modular::testing::ComponentMain<TestApp>();
  return 0;
}
