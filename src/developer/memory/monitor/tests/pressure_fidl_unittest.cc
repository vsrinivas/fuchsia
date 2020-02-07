// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/default.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <gtest/gtest.h>

#include "src/developer/memory/monitor/pressure.h"

namespace monitor {
namespace test {

namespace fmp = fuchsia::memorypressure;

class PressureFidlUnitTest : public gtest::TestLoopFixture {
 public:
  PressureFidlUnitTest()
      : context_provider_(),
        context_(context_provider_.TakeContext()),
        pressure_(false, context_.get(), async_get_default_dispatcher()) {}

 protected:
  fmp::ProviderPtr Provider() {
    fmp::ProviderPtr provider;
    context_provider_.ConnectToPublicService(provider.NewRequest());
    return provider;
  }

  void InitialLevel() {
    ASSERT_EQ(pressure_.InitMemPressureEvents(), ZX_OK);
    pressure_.WaitOnLevelChange();
  }

  void VerifyNoWatchers() { EXPECT_EQ(pressure_.watchers_.size(), 0ul); }

 private:
  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<sys::ComponentContext> context_;
  Pressure pressure_;
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

  void AddBinding(fidl::InterfaceRequest<Watcher> request) {
    bindings_.AddBinding(this, std::move(request));
  }

  int NumChanges() const { return changes_; }

 private:
  fidl::BindingSet<Watcher> bindings_;
  int changes_ = 0;
  bool send_responses_;
  OnLevelChangedCallback stashed_cb_;
};

TEST_F(PressureFidlUnitTest, Watcher) {
  // Scoped so that the Watcher gets deleted. We can then verify that the Provider has no watchers
  // remaining.
  {
    PressureWatcherForTest watcher(true);
    fmp::WatcherPtr watcher_ptr;
    watcher.AddBinding(watcher_ptr.NewRequest());

    // Registering the watcher should call OnLevelChanged().
    Provider()->RegisterWatcher(watcher_ptr.Unbind());
    RunLoopUntilIdle();
    ASSERT_EQ(watcher.NumChanges(), 1);

    // Trigger first pressure level change, causing another call to OnLevelChanged().
    InitialLevel();
    RunLoopUntilIdle();
    ASSERT_EQ(watcher.NumChanges(), 2);
  }

  RunLoopUntilIdle();
  VerifyNoWatchers();
}

TEST_F(PressureFidlUnitTest, NoResponse) {
  PressureWatcherForTest watcher(false);
  fmp::WatcherPtr watcher_ptr;
  watcher.AddBinding(watcher_ptr.NewRequest());

  Provider()->RegisterWatcher(watcher_ptr.Unbind());
  RunLoopUntilIdle();
  ASSERT_EQ(watcher.NumChanges(), 1);

  // This should not trigger a new notification as the watcher has not responded to the last one.
  InitialLevel();
  RunLoopUntilIdle();
  ASSERT_EQ(watcher.NumChanges(), 1);
}

TEST_F(PressureFidlUnitTest, DelayedResponse) {
  PressureWatcherForTest watcher(false);
  fmp::WatcherPtr watcher_ptr;
  watcher.AddBinding(watcher_ptr.NewRequest());

  Provider()->RegisterWatcher(watcher_ptr.Unbind());
  RunLoopUntilIdle();
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

}  // namespace test
}  // namespace monitor
