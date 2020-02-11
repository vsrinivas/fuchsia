// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "status_watcher.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/syslog/global.h>
#include <lib/zx/event.h>

#include <queue>
#include <thread>

#include <fbl/mutex.h>
#include <zxtest/zxtest.h>

namespace network {
namespace testing {

using network::internal::StatusWatcher;

constexpr netdev::StatusFlags kStatusOnline = netdev::StatusFlags::ONLINE;
constexpr netdev::StatusFlags kStatusOffline = netdev::StatusFlags();

status_t MakeStatus(netdev::StatusFlags status_flags, uint32_t mtu) {
  status_t ret;
  ret.flags = static_cast<uint32_t>(status_flags);
  ret.mtu = mtu;
  return ret;
}

class ObservedStatus {
 public:
  explicit ObservedStatus(const netdev::Status& status)
      : mtu_(status.mtu()), flags_(status.flags()) {}

  uint32_t mtu() const { return mtu_; }

  const netdev::StatusFlags& flags() const { return flags_; }

 private:
  uint32_t mtu_;
  netdev::StatusFlags flags_;
};

class WatchClient {
 public:
  static constexpr uint32_t kEvent = ZX_USER_SIGNAL_0;

  explicit WatchClient(zx::channel chan) : channel_(std::move(chan)), running_(true) {
    ASSERT_OK(zx::event::create(0, &event_));

    thread_ = std::thread([this]() { Thread(); });
  }

  ~WatchClient() {
    channel_.reset();
    event_.reset();
    thread_.join();
  }

  fit::optional<ObservedStatus> PopObserved() {
    fbl::AutoLock lock(&lock_);
    fit::optional<ObservedStatus> ret;
    if (!observed_status_.empty()) {
      ret = observed_status_.front();
      observed_status_.pop();
    }
    return ret;
  }

  void WaitForEventAndReset() {
    ASSERT_OK(event_.wait_one(kEvent, zx::time::infinite(), nullptr));
    event_.signal(kEvent, 0);
  }

  void NoEvents(zx::duration duration) {
    ASSERT_EQ(event_.wait_one(kEvent, zx::deadline_after(duration), nullptr), ZX_ERR_TIMED_OUT);
  }

  void WaitForEventCount(size_t count) {
    for (;;) {
      {
        fbl::AutoLock lock(&lock_);
        if (observed_status_.size() >= count) {
          return;
        }
      }
      WaitForEventAndReset();
    }
  }

 private:
  void Thread() {
    for (;;) {
      auto result = netdev::StatusWatcher::Call::WatchStatus(zx::unowned_channel(channel_));
      {
        fbl::AutoLock lock(&lock_);
        event_.signal(0, kEvent);
        if (result.ok()) {
          observed_status_.emplace(result.value().device_status);
        } else {
          break;
        }
      }
    }
    {
      fbl::AutoLock lock(&lock_);
      running_ = false;
    }
  }

  fbl::Mutex lock_;
  zx::channel channel_;
  zx::event event_;
  std::thread thread_;
  std::queue<ObservedStatus> observed_status_ __TA_GUARDED(lock_);
  bool running_ __TA_GUARDED(lock_);
};

class StatusWatcherTest : public zxtest::Test {
 public:
  StatusWatcherTest() : loop_(&kAsyncLoopConfigNeverAttachToThread) {}

  void SetUp() override {
    ASSERT_OK(loop_.StartThread("test-thread", nullptr));
    fx_logger_config_t log_cfg = {
        .min_severity = -2,
        .console_fd = dup(STDOUT_FILENO),
        .log_service_channel = ZX_HANDLE_INVALID,
        .tags = nullptr,
        .num_tags = 0,
    };
    fx_log_init_with_config(&log_cfg);
  }

  void TearDown() override {
    sync_completion_t completion;
    bool wait_for_watchers;
    {
      fbl::AutoLock lock(&lock_);
      teardown_completion_ = &completion;
      wait_for_watchers = !watchers_.is_empty();
      for (auto& w : watchers_) {
        w.Unbind();
      }
    }
    if (wait_for_watchers) {
      // Wait for all watchers to be safely unbound and destroyed.
      sync_completion_wait(&completion, ZX_TIME_INFINITE);
    }
  }

 protected:
  // Creates a watcher and returns an unowned pointer to it for interaction.
  // The test fixture will destroy the created watcher objects when they are unbound.
  StatusWatcher* MakeWatcher(zx::channel channel, uint32_t buffers,
                             fit::callback<void(StatusWatcher*)> on_closed = nullptr) {
    fbl::AutoLock lock(&lock_);
    auto watcher = std::make_unique<StatusWatcher>(buffers);
    zx_status_t status = watcher->Bind(
        loop_.dispatcher(), std::move(channel),
        [this, callback = std::move(on_closed)](StatusWatcher* closed_watcher) mutable {
          if (callback) {
            callback(closed_watcher);
          }
          fbl::AutoLock lock(&lock_);
          watchers_.erase(*closed_watcher);
          if (teardown_completion_ && watchers_.is_empty()) {
            sync_completion_signal(teardown_completion_);
          }
        });
    if (status != ZX_OK) {
      ADD_FATAL_FAILURE("Failed to bind watcher: %s", zx_status_get_string(status));
    }
    auto* unowned = watcher.get();
    watchers_.push_back(std::move(watcher));
    return unowned;
  }

  async::Loop loop_;
  fbl::Mutex lock_;
  fbl::DoublyLinkedList<std::unique_ptr<StatusWatcher>> watchers_ __TA_GUARDED(lock_);
  sync_completion_t* teardown_completion_ __TA_GUARDED(lock_);
};

TEST_F(StatusWatcherTest, HangsForStatus) {
  zx::channel ch, req;
  ASSERT_OK(zx::channel::create(0, &ch, &req));
  WatchClient cli(std::move(ch));
  ASSERT_NO_FATAL_FAILURES();
  StatusWatcher* watcher;
  ASSERT_NO_FATAL_FAILURES(watcher = MakeWatcher(std::move(req), 2));
  // ensure that the client is hanging waiting for a new status observation.
  ASSERT_NO_FATAL_FAILURES(cli.NoEvents(zx::msec(10)));
  for (int i = 0; i < 100; i++) {
    ASSERT_FALSE(cli.PopObserved().has_value());
  }
}

TEST_F(StatusWatcherTest, SingleStatus) {
  zx::channel ch, req;
  ASSERT_OK(zx::channel::create(0, &ch, &req));
  WatchClient cli(std::move(ch));
  ASSERT_NO_FATAL_FAILURES();
  StatusWatcher* watcher;
  ASSERT_NO_FATAL_FAILURES(watcher = MakeWatcher(std::move(req), 1));
  watcher->PushStatus(MakeStatus(kStatusOnline, 100));
  cli.WaitForEventAndReset();
  auto opt = cli.PopObserved();
  ASSERT_TRUE(opt.has_value());
  auto status = opt.value();
  ASSERT_EQ(status.mtu(), 100);
  ASSERT_TRUE(status.flags() & netdev::StatusFlags::ONLINE);
}

TEST_F(StatusWatcherTest, QueuesStatus) {
  zx::channel ch, req;
  ASSERT_OK(zx::channel::create(0, &ch, &req));
  StatusWatcher* watcher;
  ASSERT_NO_FATAL_FAILURES(watcher = MakeWatcher(std::move(req), 3));
  watcher->PushStatus(MakeStatus(kStatusOnline, 100));
  watcher->PushStatus(MakeStatus(kStatusOnline, 200));
  watcher->PushStatus(MakeStatus(kStatusOnline, 300));
  WatchClient cli(std::move(ch));
  ASSERT_NO_FATAL_FAILURES();
  cli.WaitForEventCount(3);
  auto opt = cli.PopObserved();
  ASSERT_TRUE(opt.has_value());
  auto status = opt.value();
  EXPECT_EQ(status.mtu(), 100);
  EXPECT_TRUE(status.flags() & netdev::StatusFlags::ONLINE);
  opt = cli.PopObserved();
  ASSERT_TRUE(opt.has_value());
  status = opt.value();
  EXPECT_EQ(status.mtu(), 200);
  EXPECT_TRUE(status.flags() & netdev::StatusFlags::ONLINE);
  opt = cli.PopObserved();
  ASSERT_TRUE(opt.has_value());
  status = opt.value();
  EXPECT_EQ(status.mtu(), 300);
  EXPECT_TRUE(status.flags() & netdev::StatusFlags::ONLINE);
}

TEST_F(StatusWatcherTest, DropsOldestStatus) {
  zx::channel ch, req;
  ASSERT_OK(zx::channel::create(0, &ch, &req));
  StatusWatcher* watcher;
  ASSERT_NO_FATAL_FAILURES(watcher = MakeWatcher(std::move(req), 2));
  watcher->PushStatus(MakeStatus(kStatusOnline, 100));
  watcher->PushStatus(MakeStatus(kStatusOnline, 200));
  watcher->PushStatus(MakeStatus(kStatusOnline, 300));
  WatchClient cli(std::move(ch));
  ASSERT_NO_FATAL_FAILURES();
  cli.WaitForEventCount(2);
  auto opt = cli.PopObserved();
  ASSERT_TRUE(opt.has_value());
  auto status = opt.value();
  ASSERT_EQ(status.mtu(), 200);
  ASSERT_TRUE(status.flags() & netdev::StatusFlags::ONLINE);
  opt = cli.PopObserved();
  ASSERT_TRUE(opt.has_value());
  status = opt.value();
  ASSERT_EQ(status.mtu(), 300);
  ASSERT_TRUE(status.flags() & netdev::StatusFlags::ONLINE);
  ASSERT_FALSE(cli.PopObserved().has_value());
}

TEST_F(StatusWatcherTest, CallsOnClosedCallback) {
  zx::channel ch, req;
  ASSERT_OK(zx::channel::create(0, &ch, &req));

  StatusWatcher* watcher;
  sync_completion_t completion;
  ASSERT_NO_FATAL_FAILURES(
      watcher = MakeWatcher(std::move(req), 2, [&completion](StatusWatcher* watcher) {
        sync_completion_signal(&completion);
      }));

  // close the channel:
  ch.reset();
  sync_completion_wait(&completion, ZX_TIME_INFINITE);
}

TEST_F(StatusWatcherTest, LockStepWatch) {
  // Tests that if everytime a status is pushed a waiter is already registered (no queuing ever
  // happens), StatusWatcher beahves appropriately.
  zx::channel ch, req;
  ASSERT_OK(zx::channel::create(0, &ch, &req));
  StatusWatcher* watcher;
  ASSERT_NO_FATAL_FAILURES(watcher = MakeWatcher(std::move(req), 2));
  // push an initial status
  watcher->PushStatus(MakeStatus(kStatusOnline, 100));
  WatchClient cli(std::move(ch));
  ASSERT_NO_FATAL_FAILURES();
  cli.WaitForEventAndReset();
  auto evt = cli.PopObserved();
  ASSERT_TRUE(evt.has_value());
  EXPECT_TRUE(evt->flags() & netdev::StatusFlags::ONLINE);
  // wait for a bit to guarantee that WatchClient is hanging.
  ASSERT_NO_FATAL_FAILURES(cli.NoEvents(zx::msec(10)));
  watcher->PushStatus(MakeStatus(kStatusOffline, 100));
  ASSERT_NO_FATAL_FAILURES(cli.WaitForEventAndReset());
  evt = cli.PopObserved();
  ASSERT_TRUE(evt.has_value());
  EXPECT_FALSE(evt->flags() & netdev::StatusFlags::ONLINE);

  // go again with status set to online.
  ASSERT_NO_FATAL_FAILURES(cli.NoEvents(zx::msec(10)));
  watcher->PushStatus(MakeStatus(kStatusOnline, 100));
  ASSERT_NO_FATAL_FAILURES(cli.WaitForEventAndReset());
  evt = cli.PopObserved();
  ASSERT_TRUE(evt.has_value());
  EXPECT_TRUE(evt->flags() & netdev::StatusFlags::ONLINE);
}

TEST_F(StatusWatcherTest, IgnoresDuplicateStatus) {
  // Tests that if PushStatus is called twice with the same status, only one event is generated.
  zx::channel ch, req;
  ASSERT_OK(zx::channel::create(0, &ch, &req));
  StatusWatcher* watcher;
  ASSERT_NO_FATAL_FAILURES(watcher = MakeWatcher(std::move(req), 2));
  // push an initial status twice
  watcher->PushStatus(MakeStatus(kStatusOnline, 100));
  watcher->PushStatus(MakeStatus(kStatusOnline, 100));
  WatchClient cli(std::move(ch));
  ASSERT_NO_FATAL_FAILURES();
  cli.WaitForEventAndReset();
  auto evt = cli.PopObserved();

  // wait for a bit to guarantee that WatchClient is hanging.
  ASSERT_NO_FATAL_FAILURES(cli.NoEvents(zx::msec(10)));
  // push a different status.
  watcher->PushStatus(MakeStatus(kStatusOffline, 100));
  ASSERT_NO_FATAL_FAILURES(cli.WaitForEventAndReset());
  evt = cli.PopObserved();
  ASSERT_TRUE(evt.has_value());
  EXPECT_FALSE(evt->flags() & netdev::StatusFlags::ONLINE);
}

}  // namespace testing
}  // namespace network
