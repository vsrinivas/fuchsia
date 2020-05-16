// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/monitor/pressure_notifier.h"

#include <lib/async/default.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <gtest/gtest.h>

namespace monitor {
namespace test {

namespace fmp = fuchsia::memorypressure;

class PressureNotifierUnitTest : public gtest::TestLoopFixture {
 public:
  PressureNotifierUnitTest()
      : context_provider_(),
        context_(context_provider_.TakeContext()),
        notifier_(false, context_.get(), async_get_default_dispatcher()) {}

 protected:
  fmp::ProviderPtr Provider() {
    fmp::ProviderPtr provider;
    context_provider_.ConnectToPublicService(provider.NewRequest());
    return provider;
  }

  void InitialLevel() { notifier_.observer_.WaitOnLevelChange(); }

  int GetWatcherCount() { return notifier_.watchers_.size(); }

  void ReleaseWatchers() {
    for (auto &w : notifier_.watchers_) {
      notifier_.ReleaseWatcher(w->proxy.get());
    }
  }

 private:
  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<sys::ComponentContext> context_;
  PressureNotifier notifier_;
};

class PressureWatcherForTest : public fmp::Watcher {
 public:
  PressureWatcherForTest(bool send_responses) : send_responses_(send_responses) {}

  ~PressureWatcherForTest() override { bindings_.CloseAll(); }

  void OnLevelChanged(fmp::Level level, OnLevelChangedCallback cb) override {
    changes_++;
    if (send_responses_) {
      cb();
    } else {
      stashed_cb_ = std::move(cb);
    }
  }

  void Respond() { stashed_cb_(); }

  void Register(fmp::ProviderPtr provider) {
    bindings_.AddBinding(this, watcher_ptr_.NewRequest());
    provider->RegisterWatcher(watcher_ptr_.Unbind());
  }

  int NumChanges() const { return changes_; }

 private:
  fidl::BindingSet<Watcher> bindings_;
  int changes_ = 0;
  bool send_responses_;
  fmp::WatcherPtr watcher_ptr_;
  OnLevelChangedCallback stashed_cb_;
};

TEST_F(PressureNotifierUnitTest, Watcher) {
  // Scoped so that the Watcher gets deleted. We can then verify that the Provider has no watchers
  // remaining.
  {
    PressureWatcherForTest watcher(true);

    // Registering the watcher should call OnLevelChanged().
    watcher.Register(Provider());
    RunLoopUntilIdle();
    ASSERT_EQ(GetWatcherCount(), 1);
    ASSERT_EQ(watcher.NumChanges(), 1);

    // Trigger first pressure level change, causing another call to OnLevelChanged().
    InitialLevel();
    RunLoopUntilIdle();
    ASSERT_EQ(watcher.NumChanges(), 2);
  }

  RunLoopUntilIdle();
  ASSERT_EQ(GetWatcherCount(), 0);
}

TEST_F(PressureNotifierUnitTest, NoResponse) {
  PressureWatcherForTest watcher(false);

  watcher.Register(Provider());
  RunLoopUntilIdle();
  ASSERT_EQ(GetWatcherCount(), 1);
  ASSERT_EQ(watcher.NumChanges(), 1);

  // This should not trigger a new notification as the watcher has not responded to the last one.
  InitialLevel();
  RunLoopUntilIdle();
  ASSERT_EQ(watcher.NumChanges(), 1);
}

TEST_F(PressureNotifierUnitTest, DelayedResponse) {
  PressureWatcherForTest watcher(false);

  watcher.Register(Provider());
  RunLoopUntilIdle();
  ASSERT_EQ(GetWatcherCount(), 1);
  ASSERT_EQ(watcher.NumChanges(), 1);

  // This should not trigger a new notification as the watcher has not responded to the last one.
  InitialLevel();
  RunLoopUntilIdle();
  ASSERT_EQ(watcher.NumChanges(), 1);

  // Respond to the last message. This should send a new notification to the watcher.
  watcher.Respond();
  RunLoopUntilIdle();
  ASSERT_EQ(watcher.NumChanges(), 2);
}

TEST_F(PressureNotifierUnitTest, MultipleWatchers) {
  // Scoped so that the Watcher gets deleted. We can then verify that the Provider has no watchers
  // remaining.
  {
    PressureWatcherForTest watcher1(true);
    PressureWatcherForTest watcher2(true);

    // Registering the watchers should call OnLevelChanged().
    watcher1.Register(Provider());
    watcher2.Register(Provider());
    RunLoopUntilIdle();
    ASSERT_EQ(GetWatcherCount(), 2);
    ASSERT_EQ(watcher1.NumChanges(), 1);
    ASSERT_EQ(watcher2.NumChanges(), 1);

    // Trigger first pressure level change, causing another call to OnLevelChanged().
    InitialLevel();
    RunLoopUntilIdle();
    ASSERT_EQ(watcher1.NumChanges(), 2);
    ASSERT_EQ(watcher2.NumChanges(), 2);
  }

  RunLoopUntilIdle();
  ASSERT_EQ(GetWatcherCount(), 0);
}

TEST_F(PressureNotifierUnitTest, MultipleWatchersNoResponse) {
  PressureWatcherForTest watcher1(false);
  PressureWatcherForTest watcher2(false);

  watcher1.Register(Provider());
  watcher2.Register(Provider());
  RunLoopUntilIdle();
  ASSERT_EQ(GetWatcherCount(), 2);
  ASSERT_EQ(watcher1.NumChanges(), 1);
  ASSERT_EQ(watcher2.NumChanges(), 1);

  // This should not trigger new notifications as the watchers have not responded to the last one.
  InitialLevel();
  RunLoopUntilIdle();
  ASSERT_EQ(watcher1.NumChanges(), 1);
  ASSERT_EQ(watcher2.NumChanges(), 1);
}

TEST_F(PressureNotifierUnitTest, MultipleWatchersDelayedResponse) {
  PressureWatcherForTest watcher1(false);
  PressureWatcherForTest watcher2(false);

  watcher1.Register(Provider());
  watcher2.Register(Provider());
  RunLoopUntilIdle();
  ASSERT_EQ(GetWatcherCount(), 2);
  ASSERT_EQ(watcher1.NumChanges(), 1);
  ASSERT_EQ(watcher2.NumChanges(), 1);

  // This should not trigger new notifications as the watchers have not responded to the last one.
  InitialLevel();
  RunLoopUntilIdle();
  ASSERT_EQ(watcher1.NumChanges(), 1);
  ASSERT_EQ(watcher2.NumChanges(), 1);

  // Respond to the last message. This should send new notifications to the watchers.
  watcher1.Respond();
  watcher2.Respond();
  RunLoopUntilIdle();
  ASSERT_EQ(watcher1.NumChanges(), 2);
  ASSERT_EQ(watcher2.NumChanges(), 2);
}

TEST_F(PressureNotifierUnitTest, MultipleWatchersMixedResponse) {
  // Set up watcher1 to not respond immediately, and watcher2 to respond immediately.
  PressureWatcherForTest watcher1(false);
  PressureWatcherForTest watcher2(true);

  watcher1.Register(Provider());
  watcher2.Register(Provider());
  RunLoopUntilIdle();
  ASSERT_EQ(GetWatcherCount(), 2);
  ASSERT_EQ(watcher1.NumChanges(), 1);
  ASSERT_EQ(watcher2.NumChanges(), 1);

  // Trigger first pressure level change.
  InitialLevel();
  RunLoopUntilIdle();
  // Since watcher1 did not respond to the previous change, it will not see this change.
  ASSERT_EQ(watcher1.NumChanges(), 1);
  // Since watcher2 responded to the previous change, it will see it.
  ASSERT_EQ(watcher2.NumChanges(), 2);

  // watcher1 responds now.
  watcher1.Respond();
  RunLoopUntilIdle();
  // watcher1 sees the previous change now.
  ASSERT_EQ(watcher1.NumChanges(), 2);
  ASSERT_EQ(watcher2.NumChanges(), 2);
}

TEST_F(PressureNotifierUnitTest, ReleaseWatcherNoPendingCallback) {
  PressureWatcherForTest watcher(true);

  watcher.Register(Provider());
  RunLoopUntilIdle();
  ASSERT_EQ(GetWatcherCount(), 1);
  ASSERT_EQ(watcher.NumChanges(), 1);

  // Trigger first pressure level change, causing another call to OnLevelChanged().
  InitialLevel();
  RunLoopUntilIdle();
  ASSERT_EQ(watcher.NumChanges(), 2);

  // Release all registered watchers, so that the watcher is now invalid.
  ReleaseWatchers();
  RunLoopUntilIdle();
  // There were no outstanding callbacks, so ReleaseWatchers() sould have freed all watchers.
  ASSERT_EQ(GetWatcherCount(), 0);
}

TEST_F(PressureNotifierUnitTest, ReleaseWatcherPendingCallback) {
  PressureWatcherForTest watcher(false);

  watcher.Register(Provider());
  RunLoopUntilIdle();
  ASSERT_EQ(GetWatcherCount(), 1);
  ASSERT_EQ(watcher.NumChanges(), 1);

  // This should not trigger a new notification as the watcher has not responded to the last one.
  InitialLevel();
  RunLoopUntilIdle();
  ASSERT_EQ(watcher.NumChanges(), 1);

  // Release all registered watchers, so that the watcher is now invalid.
  ReleaseWatchers();
  RunLoopUntilIdle();
  // Verify that the watcher has not been freed yet, since a callback is outstanding.
  ASSERT_EQ(GetWatcherCount(), 1);

  // Respond now. This should free the watcher as well.
  watcher.Respond();
  RunLoopUntilIdle();
  // Verify that the watcher has been freed.
  ASSERT_EQ(GetWatcherCount(), 0);
}

}  // namespace test
}  // namespace monitor
