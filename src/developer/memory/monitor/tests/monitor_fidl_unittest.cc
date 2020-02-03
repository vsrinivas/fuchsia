// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <gtest/gtest.h>

#include "src/developer/memory/monitor/monitor.h"

namespace monitor {
namespace test {

using namespace fuchsia::memory;
using namespace memory;
using namespace monitor;

class MonitorFidlUnitTest : public gtest::TestLoopFixture {
 protected:
  MonitorFidlUnitTest()
      : monitor_(std::make_unique<Monitor>(context_provider_.TakeContext(), fxl::CommandLine{},
                                           dispatcher(), false, false)) {}

  void TearDown() override {
    monitor_.reset();
    TestLoopFixture::TearDown();
  }

  MonitorPtr monitor() {
    MonitorPtr monitor;
    context_provider_.ConnectToPublicService(monitor.NewRequest());
    return monitor;
  }

 private:
  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<Monitor> monitor_;
};

class WatcherForTest : public fuchsia::memory::Watcher {
 public:
  WatcherForTest(fit::function<void(uint64_t free_bytes)> on_change)
      : on_change_(std::move(on_change)) {}

  void OnChange(Stats stats) override { on_change_(stats.free_bytes); }

  void AddBinding(fidl::InterfaceRequest<Watcher> request) {
    bindings_.AddBinding(this, std::move(request));
  }

 private:
  fidl::BindingSet<Watcher> bindings_;
  fit::function<void(uint64_t free_bytes)> on_change_;
};

TEST_F(MonitorFidlUnitTest, FreeBytes) {
  bool got_free = false;
  WatcherForTest watcher([&got_free](uint64_t free_bytes) { got_free = true; });
  WatcherPtr watcher_ptr;
  watcher.AddBinding(watcher_ptr.NewRequest());

  monitor()->Watch(watcher_ptr.Unbind());
  RunLoopUntilIdle();
  EXPECT_TRUE(got_free);
}

}  // namespace test
}  // namespace monitor
