// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "status_watcher.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/sync/cpp/completion.h>
#include <lib/syslog/global.h>
#include <lib/zx/event.h>

#include <queue>

#include <fbl/mutex.h>
#include <gtest/gtest.h>

#include "src/lib/testing/predicates/status.h"

namespace network {
namespace testing {

using netdev::wire::StatusFlags;
using network::internal::StatusWatcher;

constexpr StatusFlags kStatusOnline = StatusFlags::kOnline;
constexpr StatusFlags kStatusOffline = StatusFlags();

port_status_t MakeStatus(StatusFlags status_flags, uint32_t mtu) {
  return {
      .mtu = mtu,
      .flags = static_cast<uint32_t>(status_flags),
  };
}

class ObservedStatus {
 public:
  explicit ObservedStatus(const netdev::wire::PortStatus& status)
      : mtu_(status.mtu()), flags_(status.flags()) {}

  uint32_t mtu() const { return mtu_; }

  const StatusFlags& flags() const { return flags_; }

 private:
  uint32_t mtu_;
  StatusFlags flags_;
};

class WatchClient {
 public:
  static constexpr uint32_t kEvent = ZX_USER_SIGNAL_0;

  WatchClient(const WatchClient&) = delete;
  // We capture references to this in |WatchStatus|.
  WatchClient(WatchClient&&) = delete;

  // Creates a |WatchClient|. The client must only be destroyed outside of the dispatcher thread.
  static zx::result<std::unique_ptr<WatchClient>> Create(
      async_dispatcher_t* dispatcher, fidl::ClientEnd<netdev::StatusWatcher> client_end) {
    zx::event event;
    if (zx_status_t status = zx::event::create(0, &event); status != ZX_OK) {
      return zx::error(status);
    }

    std::unique_ptr ptr = std::unique_ptr<WatchClient>(
        new WatchClient(dispatcher, std::move(client_end), std::move(event)));
    return zx::ok(std::move(ptr));
  }

  std::optional<ObservedStatus> PopObserved() {
    fbl::AutoLock lock(&lock_);
    std::optional<ObservedStatus> ret;
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
    ASSERT_STATUS(event_.wait_one(kEvent, zx::deadline_after(duration), nullptr), ZX_ERR_TIMED_OUT);
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

  ~WatchClient() {
    // Wait until any |WatchStatus| callback completes.
    channel_.AsyncTeardown();
    status_watcher_client_torn_down_.Wait();
  }

 private:
  explicit WatchClient(async_dispatcher_t* dispatcher,
                       fidl::ClientEnd<netdev::StatusWatcher> client_end, zx::event event)
      : channel_(std::move(client_end), dispatcher,
                 fidl::ObserveTeardown([this] { status_watcher_client_torn_down_.Signal(); })),
        event_(std::move(event)) {
    WatchStatus();
  }

  void WatchStatus() {
    channel_->WatchStatus().Then(
        [this](fidl::WireUnownedResult<netdev::StatusWatcher::WatchStatus>& result) {
          if (!result.ok()) {
            return;
          }
          auto* resp = result.Unwrap();
          fbl::AutoLock lock(&lock_);
          EXPECT_OK(event_.signal(0, kEvent));
          observed_status_.emplace(resp->port_status);
          WatchStatus();
        });
  }

  fbl::Mutex lock_;
  fidl::WireSharedClient<netdev::StatusWatcher> channel_;
  zx::event event_;
  std::queue<ObservedStatus> observed_status_ __TA_GUARDED(lock_);
  libsync::Completion status_watcher_client_torn_down_;
};

class StatusWatcherTest : public ::testing::Test {
 public:
  StatusWatcherTest() : loop_(&kAsyncLoopConfigNeverAttachToThread) {}

  void SetUp() override {
    ASSERT_OK(loop_.StartThread("test-thread", nullptr));
    fx_logger_config_t log_cfg = {
        .min_severity = -2,
        .tags = nullptr,
        .num_tags = 0,
    };
    fx_log_reconfigure(&log_cfg);
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
  zx::result<StatusWatcher*> MakeWatcher(fidl::ServerEnd<netdev::StatusWatcher> channel,
                                         uint32_t buffers,
                                         fit::callback<void(StatusWatcher*)> on_closed = nullptr) {
    fbl::AutoLock lock(&lock_);
    std::unique_ptr watcher = std::make_unique<StatusWatcher>(buffers);
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
      return zx::error(status);
    }
    StatusWatcher* unowned = watcher.get();
    watchers_.push_back(std::move(watcher));
    return zx::ok(unowned);
  }

  async::Loop loop_;
  fbl::Mutex lock_;
  fbl::DoublyLinkedList<std::unique_ptr<StatusWatcher>> watchers_ __TA_GUARDED(lock_);
  sync_completion_t* teardown_completion_ __TA_GUARDED(lock_) = nullptr;
};

TEST_F(StatusWatcherTest, HangsForStatus) {
  zx::result endpoints = fidl::CreateEndpoints<netdev::StatusWatcher>();
  ASSERT_OK(endpoints.status_value());
  auto [ch, req] = std::move(*endpoints);
  zx::result watch_client_creation = WatchClient::Create(loop_.dispatcher(), std::move(ch));
  ASSERT_OK(watch_client_creation.status_value());
  WatchClient& cli = *watch_client_creation.value();
  // ensure that the client is hanging waiting for a new status observation.
  ASSERT_NO_FATAL_FAILURE(cli.NoEvents(zx::msec(10)));
  for (int i = 0; i < 100; i++) {
    ASSERT_FALSE(cli.PopObserved().has_value());
  }
}

TEST_F(StatusWatcherTest, SingleStatus) {
  zx::result endpoints = fidl::CreateEndpoints<netdev::StatusWatcher>();
  ASSERT_OK(endpoints.status_value());
  auto [ch, req] = std::move(*endpoints);
  zx::result watch_client_creation = WatchClient::Create(loop_.dispatcher(), std::move(ch));
  ASSERT_OK(watch_client_creation.status_value());
  WatchClient& cli = *watch_client_creation.value();
  zx::result watcher_creation = MakeWatcher(std::move(req), 1);
  ASSERT_OK(watcher_creation.status_value());
  StatusWatcher* watcher = watcher_creation.value();
  watcher->PushStatus(MakeStatus(kStatusOnline, 100));
  cli.WaitForEventAndReset();
  std::optional opt = cli.PopObserved();
  ASSERT_TRUE(opt.has_value());
  ObservedStatus status = opt.value();
  ASSERT_EQ(status.mtu(), 100u);
  ASSERT_TRUE(status.flags() & StatusFlags::kOnline);
}

TEST_F(StatusWatcherTest, QueuesStatus) {
  zx::result endpoints = fidl::CreateEndpoints<netdev::StatusWatcher>();
  ASSERT_OK(endpoints.status_value());
  auto [ch, req] = std::move(*endpoints);
  zx::result watcher_creation = MakeWatcher(std::move(req), 3);
  ASSERT_OK(watcher_creation.status_value());
  StatusWatcher* watcher = watcher_creation.value();
  watcher->PushStatus(MakeStatus(kStatusOnline, 100));
  watcher->PushStatus(MakeStatus(kStatusOnline, 200));
  watcher->PushStatus(MakeStatus(kStatusOnline, 300));
  zx::result watch_client_creation = WatchClient::Create(loop_.dispatcher(), std::move(ch));
  ASSERT_OK(watch_client_creation.status_value());
  WatchClient& cli = *watch_client_creation.value();
  cli.WaitForEventCount(3);
  {
    std::optional opt = cli.PopObserved();
    ASSERT_TRUE(opt.has_value());
    ObservedStatus status = opt.value();
    EXPECT_EQ(status.mtu(), 100u);
    EXPECT_TRUE(status.flags() & StatusFlags::kOnline);
  }
  {
    std::optional opt = cli.PopObserved();
    ASSERT_TRUE(opt.has_value());
    ObservedStatus status = opt.value();
    EXPECT_EQ(status.mtu(), 200u);
    EXPECT_TRUE(status.flags() & StatusFlags::kOnline);
  }
  {
    std::optional opt = cli.PopObserved();
    ASSERT_TRUE(opt.has_value());
    ObservedStatus status = opt.value();
    EXPECT_EQ(status.mtu(), 300u);
    EXPECT_TRUE(status.flags() & StatusFlags::kOnline);
  }
}

TEST_F(StatusWatcherTest, DropsOldestStatus) {
  zx::result endpoints = fidl::CreateEndpoints<netdev::StatusWatcher>();
  ASSERT_OK(endpoints.status_value());
  auto [ch, req] = std::move(*endpoints);
  zx::result watcher_creation = MakeWatcher(std::move(req), 2);
  ASSERT_OK(watcher_creation.status_value());
  StatusWatcher* watcher = watcher_creation.value();
  watcher->PushStatus(MakeStatus(kStatusOnline, 100));
  watcher->PushStatus(MakeStatus(kStatusOnline, 200));
  watcher->PushStatus(MakeStatus(kStatusOnline, 300));
  zx::result watch_client_creation = WatchClient::Create(loop_.dispatcher(), std::move(ch));
  ASSERT_OK(watch_client_creation.status_value());
  WatchClient& cli = *watch_client_creation.value();
  cli.WaitForEventCount(2);
  {
    std::optional opt = cli.PopObserved();
    ASSERT_TRUE(opt.has_value());
    ObservedStatus status = opt.value();
    ASSERT_EQ(status.mtu(), 200u);
    ASSERT_TRUE(status.flags() & StatusFlags::kOnline);
  }
  {
    std::optional opt = cli.PopObserved();
    ASSERT_TRUE(opt.has_value());
    ObservedStatus status = opt.value();
    ASSERT_EQ(status.mtu(), 300u);
    ASSERT_TRUE(status.flags() & StatusFlags::kOnline);
    ASSERT_FALSE(cli.PopObserved().has_value());
  }
}

TEST_F(StatusWatcherTest, CallsOnClosedCallback) {
  zx::result endpoints = fidl::CreateEndpoints<netdev::StatusWatcher>();
  ASSERT_OK(endpoints.status_value());
  auto [ch, req] = std::move(*endpoints);

  sync_completion_t completion;
  zx::result watcher_creation =
      MakeWatcher(std::move(req), 2,
                  [&completion](StatusWatcher* watcher) { sync_completion_signal(&completion); });
  ASSERT_OK(watcher_creation.status_value());

  // close the channel:
  ch.reset();
  sync_completion_wait(&completion, ZX_TIME_INFINITE);
}

TEST_F(StatusWatcherTest, LockStepWatch) {
  // Tests that if everytime a status is pushed a waiter is already registered (no queuing ever
  // happens), StatusWatcher behaves appropriately.
  zx::result endpoints = fidl::CreateEndpoints<netdev::StatusWatcher>();
  ASSERT_OK(endpoints.status_value());
  auto [ch, req] = std::move(*endpoints);
  zx::result watcher_creation = MakeWatcher(std::move(req), 2);
  ASSERT_OK(watcher_creation.status_value());
  StatusWatcher* watcher = watcher_creation.value();
  // push an initial status
  watcher->PushStatus(MakeStatus(kStatusOnline, 100));
  zx::result watch_client_creation = WatchClient::Create(loop_.dispatcher(), std::move(ch));
  ASSERT_OK(watch_client_creation.status_value());
  WatchClient& cli = *watch_client_creation.value();
  cli.WaitForEventAndReset();
  {
    std::optional evt = cli.PopObserved();
    ASSERT_TRUE(evt.has_value());
    EXPECT_TRUE(evt->flags() & StatusFlags::kOnline);
  }
  // wait for a bit to guarantee that WatchClient is hanging.
  ASSERT_NO_FATAL_FAILURE(cli.NoEvents(zx::msec(10)));
  watcher->PushStatus(MakeStatus(kStatusOffline, 100));
  ASSERT_NO_FATAL_FAILURE(cli.WaitForEventAndReset());
  {
    std::optional evt = cli.PopObserved();
    ASSERT_TRUE(evt.has_value());
    EXPECT_FALSE(evt->flags() & StatusFlags::kOnline);
  }

  // go again with status set to online.
  ASSERT_NO_FATAL_FAILURE(cli.NoEvents(zx::msec(10)));
  watcher->PushStatus(MakeStatus(kStatusOnline, 100));
  ASSERT_NO_FATAL_FAILURE(cli.WaitForEventAndReset());
  {
    std::optional evt = cli.PopObserved();
    ASSERT_TRUE(evt.has_value());
    EXPECT_TRUE(evt->flags() & StatusFlags::kOnline);
  }
}

TEST_F(StatusWatcherTest, IgnoresDuplicateStatus) {
  // Tests that if PushStatus is called twice with the same status, only one event is generated.
  zx::result endpoints = fidl::CreateEndpoints<netdev::StatusWatcher>();
  ASSERT_OK(endpoints.status_value());
  auto [ch, req] = std::move(*endpoints);
  zx::result watcher_creation = MakeWatcher(std::move(req), 2);
  ASSERT_OK(watcher_creation.status_value());
  StatusWatcher* watcher = watcher_creation.value();
  // push an initial status twice
  watcher->PushStatus(MakeStatus(kStatusOnline, 100));
  watcher->PushStatus(MakeStatus(kStatusOnline, 100));
  zx::result watch_client_creation = WatchClient::Create(loop_.dispatcher(), std::move(ch));
  ASSERT_OK(watch_client_creation.status_value());
  WatchClient& cli = *watch_client_creation.value();
  cli.WaitForEventAndReset();

  {
    std::optional evt = cli.PopObserved();
    ASSERT_TRUE(evt.has_value());
    EXPECT_TRUE(evt->flags() & StatusFlags::kOnline);
  }

  // wait for a bit to guarantee that WatchClient is hanging.
  ASSERT_NO_FATAL_FAILURE(cli.NoEvents(zx::msec(10)));
  // push a different status.
  watcher->PushStatus(MakeStatus(kStatusOffline, 100));
  ASSERT_NO_FATAL_FAILURE(cli.WaitForEventAndReset());
  {
    std::optional evt = cli.PopObserved();
    ASSERT_TRUE(evt.has_value());
    EXPECT_FALSE(evt->flags() & StatusFlags::kOnline);
  }
}

}  // namespace testing
}  // namespace network
