// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/sync_watcher_set.h"

#include <lib/fidl/cpp/binding.h>

#include <algorithm>
#include <string>

#include "gtest/gtest.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace ledger {
namespace {

class SyncWatcherSetTest : public gtest::TestLoopFixture {
 public:
  SyncWatcherSetTest() = default;
  SyncWatcherSetTest(const SyncWatcherSetTest&) = delete;
  SyncWatcherSetTest& operator=(const SyncWatcherSetTest&) = delete;
  ~SyncWatcherSetTest() override = default;

 protected:
  void SetUp() override { ::testing::Test::SetUp(); }
};

class SyncWatcherImpl : public SyncWatcher {
 public:
  explicit SyncWatcherImpl(fidl::InterfaceRequest<SyncWatcher> request)
      : binding_(this, std::move(request)) {}
  SyncWatcherImpl(const SyncWatcherImpl&) = delete;
  SyncWatcherImpl& operator=(const SyncWatcherImpl&) = delete;

  void SyncStateChanged(SyncState download_status, SyncState upload_status,
                        SyncStateChangedCallback callback) override {
    download_states.push_back(download_status);
    upload_states.push_back(upload_status);
    callback();
  }

  std::vector<SyncState> download_states;
  std::vector<SyncState> upload_states;

 private:
  fidl::Binding<SyncWatcher> binding_;
};

TEST_F(SyncWatcherSetTest, OneWatcher) {
  SyncWatcherSet watcher_set(dispatcher());
  SyncWatcherPtr watcher_ptr;

  SyncWatcherImpl impl(watcher_ptr.NewRequest());

  watcher_set.Notify({sync_coordinator::DOWNLOAD_IN_PROGRESS, sync_coordinator::UPLOAD_PENDING});

  watcher_set.AddSyncWatcher(std::move(watcher_ptr));

  RunLoopUntilIdle();

  ASSERT_EQ(impl.download_states.size(), 1u);
  EXPECT_EQ(*impl.download_states.rbegin(), SyncState::IN_PROGRESS);
  ASSERT_EQ(impl.upload_states.size(), 1u);
  EXPECT_EQ(*impl.upload_states.rbegin(), SyncState::PENDING);

  watcher_set.Notify({sync_coordinator::DOWNLOAD_ERROR, sync_coordinator::UPLOAD_IDLE});

  RunLoopUntilIdle();

  ASSERT_EQ(impl.download_states.size(), 2u);
  EXPECT_EQ(*impl.download_states.rbegin(), SyncState::ERROR);
  ASSERT_EQ(impl.upload_states.size(), 2u);
  EXPECT_EQ(*impl.upload_states.rbegin(), SyncState::IDLE);
}

TEST_F(SyncWatcherSetTest, TwoWatchers) {
  SyncWatcherSet watcher_set(dispatcher());

  SyncWatcherPtr watcher_ptr1;
  SyncWatcherImpl impl1(watcher_ptr1.NewRequest());
  watcher_set.AddSyncWatcher(std::move(watcher_ptr1));

  RunLoopUntilIdle();
  EXPECT_EQ(impl1.download_states.size(), 1u);
  EXPECT_EQ(*impl1.download_states.rbegin(), SyncState::IDLE);
  EXPECT_EQ(impl1.upload_states.size(), 1u);
  EXPECT_EQ(*impl1.upload_states.rbegin(), SyncState::IDLE);

  SyncWatcherPtr watcher_ptr2;
  SyncWatcherImpl impl2(watcher_ptr2.NewRequest());
  watcher_set.AddSyncWatcher(std::move(watcher_ptr2));

  RunLoopUntilIdle();
  EXPECT_EQ(impl2.download_states.size(), 1u);
  EXPECT_EQ(*impl2.download_states.rbegin(), SyncState::IDLE);
  EXPECT_EQ(impl2.upload_states.size(), 1u);
  EXPECT_EQ(*impl2.upload_states.rbegin(), SyncState::IDLE);

  watcher_set.Notify({sync_coordinator::DOWNLOAD_IN_PROGRESS, sync_coordinator::UPLOAD_PENDING});

  RunLoopUntilIdle();

  ASSERT_EQ(impl1.download_states.size(), 2u);
  EXPECT_EQ(*impl1.download_states.rbegin(), SyncState::IN_PROGRESS);
  ASSERT_EQ(impl1.upload_states.size(), 2u);
  EXPECT_EQ(*impl1.upload_states.rbegin(), SyncState::PENDING);

  ASSERT_EQ(impl2.download_states.size(), 2u);
  EXPECT_EQ(*impl2.download_states.rbegin(), SyncState::IN_PROGRESS);
  ASSERT_EQ(impl2.upload_states.size(), 2u);
  EXPECT_EQ(*impl2.upload_states.rbegin(), SyncState::PENDING);
}

}  // namespace
}  // namespace ledger
