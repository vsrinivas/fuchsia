// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/environment/environment.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "peridot/lib/rng/test_random.h"
#include "src/ledger/bin/environment/test_loop_notification.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/loop_fixture/test_loop_fixture.h"
#include "src/ledger/lib/timekeeper/test_clock.h"

namespace ledger {
namespace {

class EnvironmentTest : public TestLoopFixture {
 public:
  sys::testing::ComponentContextProvider component_context_provider_;
};

TEST_F(EnvironmentTest, InitializationOfAsyncAndIOAsync) {
  auto io_loop = test_loop().StartNewLoop();
  Environment env = EnvironmentBuilder()
                        .SetStartupContext(component_context_provider_.context())
                        .SetAsync(dispatcher())
                        .SetIOAsync(io_loop->dispatcher())
                        .SetNotificationFactory(TestLoopNotification::NewFactory(&test_loop()))
                        .Build();

  EXPECT_EQ(env.dispatcher(), dispatcher());
  EXPECT_EQ(env.io_dispatcher(), io_loop->dispatcher());
}

TEST_F(EnvironmentTest, InitializationClock) {
  auto io_loop = test_loop().StartNewLoop();
  auto clock = std::make_unique<TestClock>();
  auto clock_ptr = clock.get();
  Environment env = EnvironmentBuilder()
                        .SetStartupContext(component_context_provider_.context())
                        .SetAsync(dispatcher())
                        .SetIOAsync(io_loop->dispatcher())
                        .SetNotificationFactory(TestLoopNotification::NewFactory(&test_loop()))
                        .SetClock(std::move(clock))
                        .Build();

  EXPECT_EQ(env.clock(), clock_ptr);
}

TEST_F(EnvironmentTest, InitializationRandom) {
  auto io_loop = test_loop().StartNewLoop();
  auto random = std::make_unique<rng::TestRandom>(0);
  auto random_ptr = random.get();
  Environment env = EnvironmentBuilder()
                        .SetStartupContext(component_context_provider_.context())
                        .SetAsync(dispatcher())
                        .SetIOAsync(io_loop->dispatcher())
                        .SetNotificationFactory(TestLoopNotification::NewFactory(&test_loop()))
                        .SetRandom(std::move(random))
                        .Build();

  EXPECT_EQ(env.random(), random_ptr);
}

TEST_F(EnvironmentTest, InitializationGcPolicy) {
  auto io_loop = test_loop().StartNewLoop();
  Environment env = EnvironmentBuilder()
                        .SetStartupContext(component_context_provider_.context())
                        .SetAsync(dispatcher())
                        .SetIOAsync(io_loop->dispatcher())
                        .SetNotificationFactory(TestLoopNotification::NewFactory(&test_loop()))
                        .SetGcPolicy(storage::GarbageCollectionPolicy::EAGER_LIVE_REFERENCES)
                        .Build();

  EXPECT_EQ(env.gc_policy(), storage::GarbageCollectionPolicy::EAGER_LIVE_REFERENCES);
}

TEST_F(EnvironmentTest, NotificationFactoryTest) {
  auto io_loop = test_loop().StartNewLoop();
  Environment env = EnvironmentBuilder()
                        .SetStartupContext(component_context_provider_.context())
                        .SetAsync(dispatcher())
                        .SetIOAsync(io_loop->dispatcher())
                        .SetNotificationFactory(TestLoopNotification::NewFactory(&test_loop()))
                        .Build();

  bool called = false;
  async::PostTask(env.dispatcher(), [&] {
    auto notification = env.MakeNotification();
    async::PostTask(env.io_dispatcher(), [&] { notification->Notify(); });
    notification->WaitForNotification();
    EXPECT_TRUE(notification->HasBeenNotified());
    called = true;
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
}

TEST(EnvironmentWithRealLoopTest, NotificationFactoryTest) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async::Loop io_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  io_loop.StartThread();
  sys::testing::ComponentContextProvider component_context_provider;
  Environment env = EnvironmentBuilder()
                        .SetStartupContext(component_context_provider.context())
                        .SetAsync(loop.dispatcher())
                        .SetIOAsync(io_loop.dispatcher())
                        .Build();

  bool called = false;
  async::PostTask(env.dispatcher(), [&] {
    auto notification = env.MakeNotification();
    async::PostTask(env.io_dispatcher(), [&] { notification->Notify(); });
    notification->WaitForNotification();
    EXPECT_TRUE(notification->HasBeenNotified());
    called = true;
  });
  loop.RunUntilIdle();
  EXPECT_TRUE(called);
}

TEST_F(EnvironmentTest, IniatilizationDefaultDiffCompatibilityPolicy) {
  auto io_loop = test_loop().StartNewLoop();
  Environment env = EnvironmentBuilder()
                        .SetStartupContext(component_context_provider_.context())
                        .SetAsync(dispatcher())
                        .SetIOAsync(io_loop->dispatcher())
                        .Build();

  EXPECT_EQ(env.diff_compatibility_policy(),
            storage::DiffCompatibilityPolicy::USE_DIFFS_AND_TREE_NODES);
}

TEST_F(EnvironmentTest, IniatilizationDiffCompatibilityPolicy) {
  auto io_loop = test_loop().StartNewLoop();
  Environment env =
      EnvironmentBuilder()
          .SetStartupContext(component_context_provider_.context())
          .SetAsync(dispatcher())
          .SetIOAsync(io_loop->dispatcher())
          .SetDiffCompatibilityPolicy(storage::DiffCompatibilityPolicy::USE_ONLY_DIFFS)
          .Build();

  EXPECT_EQ(env.diff_compatibility_policy(), storage::DiffCompatibilityPolicy::USE_ONLY_DIFFS);
}

}  // namespace
}  // namespace ledger
