// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/sync_watcher_set.h"

#include <algorithm>
#include <string>

#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fsl/tasks/message_loop.h"

namespace ledger {
namespace {

class SyncWatcherSetTest : public test::TestWithMessageLoop {
 public:
  SyncWatcherSetTest() {}
  ~SyncWatcherSetTest() override {}

 protected:
  void SetUp() override { ::testing::Test::SetUp(); }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(SyncWatcherSetTest);
};

class SyncWatcherImpl : public SyncWatcher {
 public:
  explicit SyncWatcherImpl(fidl::InterfaceRequest<SyncWatcher> request)
      : binding_(this, std::move(request)) {}
  void SyncStateChanged(SyncState download_status,
                        SyncState upload_status,
                        const SyncStateChangedCallback& callback) override {
    download_states.push_back(download_status);
    upload_states.push_back(upload_status);
    callback();
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
  }

  std::vector<SyncState> download_states;
  std::vector<SyncState> upload_states;

 private:
  fidl::Binding<SyncWatcher> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SyncWatcherImpl);
};

TEST_F(SyncWatcherSetTest, OneWatcher) {
  SyncWatcherSet watcher_set;
  SyncWatcherPtr watcher_ptr;

  SyncWatcherImpl impl(watcher_ptr.NewRequest());

  watcher_set.Notify(cloud_sync::CATCH_UP_DOWNLOAD,
                     cloud_sync::WAIT_CATCH_UP_DOWNLOAD);

  watcher_set.AddSyncWatcher(std::move(watcher_ptr));

  EXPECT_FALSE(RunLoopWithTimeout());

  ASSERT_EQ(1u, impl.download_states.size());
  EXPECT_EQ(SyncState::IN_PROGRESS, *impl.download_states.rbegin());
  ASSERT_EQ(1u, impl.upload_states.size());
  EXPECT_EQ(SyncState::PENDING, *impl.upload_states.rbegin());

  watcher_set.Notify(cloud_sync::DOWNLOAD_ERROR, cloud_sync::UPLOAD_IDLE);

  EXPECT_FALSE(RunLoopWithTimeout());

  ASSERT_EQ(2u, impl.download_states.size());
  EXPECT_EQ(SyncState::ERROR, *impl.download_states.rbegin());
  ASSERT_EQ(2u, impl.upload_states.size());
  EXPECT_EQ(SyncState::IDLE, *impl.upload_states.rbegin());
}

TEST_F(SyncWatcherSetTest, TwoWatchers) {
  SyncWatcherSet watcher_set;

  SyncWatcherPtr watcher_ptr1;
  SyncWatcherImpl impl1(watcher_ptr1.NewRequest());
  watcher_set.AddSyncWatcher(std::move(watcher_ptr1));

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1u, impl1.download_states.size());
  EXPECT_EQ(SyncState::IDLE, *impl1.download_states.rbegin());
  EXPECT_EQ(1u, impl1.upload_states.size());
  EXPECT_EQ(SyncState::IDLE, *impl1.upload_states.rbegin());

  SyncWatcherPtr watcher_ptr2;
  SyncWatcherImpl impl2(watcher_ptr2.NewRequest());
  watcher_set.AddSyncWatcher(std::move(watcher_ptr2));

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1u, impl2.download_states.size());
  EXPECT_EQ(SyncState::IDLE, *impl2.download_states.rbegin());
  EXPECT_EQ(1u, impl2.upload_states.size());
  EXPECT_EQ(SyncState::IDLE, *impl2.upload_states.rbegin());

  watcher_set.Notify(cloud_sync::REMOTE_COMMIT_DOWNLOAD,
                     cloud_sync::WAIT_REMOTE_DOWNLOAD);

  // The two watchers are notified.
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_FALSE(RunLoopWithTimeout());

  ASSERT_EQ(2u, impl1.download_states.size());
  EXPECT_EQ(SyncState::IN_PROGRESS, *impl1.download_states.rbegin());
  ASSERT_EQ(2u, impl1.upload_states.size());
  EXPECT_EQ(SyncState::PENDING, *impl1.upload_states.rbegin());

  ASSERT_EQ(2u, impl2.download_states.size());
  EXPECT_EQ(SyncState::IN_PROGRESS, *impl2.download_states.rbegin());
  ASSERT_EQ(2u, impl2.upload_states.size());
  EXPECT_EQ(SyncState::PENDING, *impl2.upload_states.rbegin());
}

}  // namespace
}  // namespace ledger
