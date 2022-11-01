// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/delay_watcher_server.h"

#include <fidl/fuchsia.audio/cpp/wire.h>
#include <lib/async-testing/test_loop.h>
#include <lib/sync/completion.h>

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "src/media/audio/services/common/testing/test_server_and_sync_client.h"

namespace media_audio {
namespace {

TEST(DelayWatcherServerTest, InitialDelayUnknown) {
  fidl::Arena<> arena;
  auto thread = FidlThread::CreateFromNewThread("TestFidlThread");
  auto wrapper = std::make_shared<TestServerAndWireSyncClient<DelayWatcherServer>>(
      thread, DelayWatcherServer::Args{
                  .name = "DelayWatcher",
                  .initial_delay = std::nullopt,
              });

  // First call returns immediately.
  {
    auto result = wrapper->client()->WatchDelay(
        fuchsia_audio::wire::DelayWatcherWatchDelayRequest::Builder(arena).Build());
    ASSERT_TRUE(result.ok()) << result;
    EXPECT_FALSE(result->has_delay());
  }

  // Another call shouldn't complete until there has been an update.
  libsync::Completion done;
  std::thread t([&done, wrapper]() {
    fidl::Arena<> arena;
    auto result = wrapper->client()->WatchDelay(
        fuchsia_audio::wire::DelayWatcherWatchDelayRequest::Builder(arena).Build());
    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result->has_delay());
    EXPECT_EQ(result->delay(), 1000);
    done.Signal();
  });

  EXPECT_FALSE(done.signaled());

  thread->PostTask([wrapper]() {
    ScopedThreadChecker checker(wrapper->server().thread().checker());
    wrapper->server().set_delay(zx::nsec(1000));
  });

  EXPECT_EQ(done.Wait(zx::sec(5)), ZX_OK);
  t.join();
}

TEST(DelayWatcherServerTest, InitialDelayKnown) {
  fidl::Arena<> arena;
  auto thread = FidlThread::CreateFromNewThread("TestFidlThread");
  auto wrapper = std::make_shared<TestServerAndWireSyncClient<DelayWatcherServer>>(
      thread, DelayWatcherServer::Args{
                  .name = "DelayWatcher",
                  .initial_delay = zx::nsec(1000),
              });

  // First call returns immediately.
  {
    auto result = wrapper->client()->WatchDelay(
        fuchsia_audio::wire::DelayWatcherWatchDelayRequest::Builder(arena).Build());
    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result->has_delay());
    EXPECT_EQ(result->delay(), 1000);
  }

  // Two updates without another call.
  libsync::Completion done;
  thread->PostTask([wrapper]() {
    ScopedThreadChecker checker(wrapper->server().thread().checker());
    wrapper->server().set_delay(zx::nsec(2000));
  });
  thread->PostTask([wrapper, &done]() {
    ScopedThreadChecker checker(wrapper->server().thread().checker());
    wrapper->server().set_delay(zx::nsec(3000));
    done.Signal();
  });

  EXPECT_EQ(done.Wait(zx::sec(5)), ZX_OK);

  // Second call returns immediately because there's been an update.
  {
    auto result = wrapper->client()->WatchDelay(
        fuchsia_audio::wire::DelayWatcherWatchDelayRequest::Builder(arena).Build());
    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result->has_delay());
    EXPECT_EQ(result->delay(), 3000);
  }
}

TEST(DelayWatcherServerGroupTest, Groups) {
  async::TestLoop loop;
  auto thread = FidlThread::CreateFromCurrentThread("TestFidlThread", loop.dispatcher());

  auto endpoints1 = fidl::CreateEndpoints<fuchsia_audio::DelayWatcher>();
  auto endpoints2 = fidl::CreateEndpoints<fuchsia_audio::DelayWatcher>();
  ASSERT_TRUE(endpoints1.is_ok());
  ASSERT_TRUE(endpoints2.is_ok());

  auto client1 = std::optional(fidl::WireClient(std::move(endpoints1->client), loop.dispatcher()));
  auto client2 = std::optional(fidl::WireClient(std::move(endpoints2->client), loop.dispatcher()));

  DelayWatcherServerGroup group("Group", thread);
  group.Add(std::move(endpoints1->server));
  group.Add(std::move(endpoints2->server));
  EXPECT_EQ(group.num_live_servers(), 2);

  group.set_delay(zx::nsec(100));

  fidl::Arena<> arena;
  auto request = fuchsia_audio::wire::DelayWatcherWatchDelayRequest::Builder(arena).Build();

  // Both servers should see this delay.
  {
    bool done = false;
    (*client1)->WatchDelay(request).Then([&done](auto& result) {
      ASSERT_TRUE(result.ok()) << result;
      ASSERT_TRUE(result->has_delay());
      EXPECT_EQ(result->delay(), 100);
      done = true;
    });
    loop.RunUntilIdle();
  }
  {
    bool done = false;
    (*client2)->WatchDelay(request).Then([&done](auto& result) {
      ASSERT_TRUE(result.ok()) << result;
      ASSERT_TRUE(result->has_delay());
      EXPECT_EQ(result->delay(), 100);
      done = true;
    });
    loop.RunUntilIdle();
  }

  // Teardown server1.
  client1 = std::nullopt;
  loop.RunUntilIdle();
  EXPECT_EQ(group.num_live_servers(), 1);

  group.set_delay(zx::nsec(200));

  // Check that client2 still works.
  {
    bool done = false;
    (*client2)->WatchDelay(request).Then([&done](auto& result) {
      ASSERT_TRUE(result.ok()) << result;
      ASSERT_TRUE(result->has_delay());
      EXPECT_EQ(result->delay(), 200);
      done = true;
    });
    loop.RunUntilIdle();
  }

  // Teardown server2 via group.Shutdown().
  group.Shutdown();
  loop.RunUntilIdle();
  EXPECT_EQ(group.num_live_servers(), 0);

  group.set_delay(zx::nsec(300));

  // The above update should not be visible to client2
  {
    bool done = false;
    (*client2)->WatchDelay(request).Then([&done](auto& result) {
      ASSERT_FALSE(result.ok());
      done = true;
    });
    loop.RunUntilIdle();
  }
}

}  // namespace
}  // namespace media_audio
