// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cloud_sync/impl/aggregator.h"

#include "gtest/gtest.h"
#include "src/ledger/bin/cloud_sync/public/sync_state_watcher.h"

namespace cloud_sync {

namespace {

class RecordingWatcher : public SyncStateWatcher {
 public:
  void Notify(SyncStateContainer sync_state) override { states.push_back(sync_state); }

  std::vector<SyncStateContainer> states;
};

TEST(AggregatorTest, SendFirstNotification) {
  std::unique_ptr<RecordingWatcher> base_watcher = std::make_unique<RecordingWatcher>();
  Aggregator aggregator;
  aggregator.SetBaseWatcher(base_watcher.get());

  std::unique_ptr<SyncStateWatcher> watcher1 = aggregator.GetNewStateWatcher();
  watcher1->Notify(DOWNLOAD_IN_PROGRESS, UPLOAD_WAIT_REMOTE_DOWNLOAD);

  ASSERT_EQ(2u, base_watcher->states.size());
  EXPECT_EQ(DOWNLOAD_IN_PROGRESS, base_watcher->states[1].download);
  EXPECT_EQ(UPLOAD_WAIT_REMOTE_DOWNLOAD, base_watcher->states[1].upload);
}

TEST(AggregatorTest, AggregateTwo) {
  std::unique_ptr<RecordingWatcher> base_watcher = std::make_unique<RecordingWatcher>();

  Aggregator aggregator;
  aggregator.SetBaseWatcher(base_watcher.get());

  std::unique_ptr<SyncStateWatcher> watcher1 = aggregator.GetNewStateWatcher();
  std::unique_ptr<SyncStateWatcher> watcher2 = aggregator.GetNewStateWatcher();

  EXPECT_EQ(DOWNLOAD_IDLE, base_watcher->states.rbegin()->download);
  EXPECT_EQ(UPLOAD_IDLE, base_watcher->states.rbegin()->upload);

  watcher1->Notify(DOWNLOAD_IN_PROGRESS, UPLOAD_WAIT_REMOTE_DOWNLOAD);

  EXPECT_EQ(DOWNLOAD_IN_PROGRESS, base_watcher->states.rbegin()->download);
  EXPECT_EQ(UPLOAD_WAIT_REMOTE_DOWNLOAD, base_watcher->states.rbegin()->upload);

  watcher2->Notify(DOWNLOAD_IDLE, UPLOAD_IDLE);
  EXPECT_EQ(DOWNLOAD_IN_PROGRESS, base_watcher->states.rbegin()->download);
  EXPECT_EQ(UPLOAD_WAIT_REMOTE_DOWNLOAD, base_watcher->states.rbegin()->upload);

  watcher1->Notify(DOWNLOAD_IDLE, UPLOAD_IN_PROGRESS);
  EXPECT_EQ(DOWNLOAD_IDLE, base_watcher->states.rbegin()->download);
  EXPECT_EQ(UPLOAD_IN_PROGRESS, base_watcher->states.rbegin()->upload);
}

TEST(AggregatorTest, ResetWatcher) {
  std::unique_ptr<RecordingWatcher> base_watcher = std::make_unique<RecordingWatcher>();
  Aggregator aggregator;
  aggregator.SetBaseWatcher(base_watcher.get());

  std::unique_ptr<SyncStateWatcher> watcher1 = aggregator.GetNewStateWatcher();
  watcher1->Notify(DOWNLOAD_IN_PROGRESS, UPLOAD_WAIT_REMOTE_DOWNLOAD);

  ASSERT_EQ(2u, base_watcher->states.size());
  EXPECT_EQ(DOWNLOAD_IN_PROGRESS, base_watcher->states[1].download);
  EXPECT_EQ(UPLOAD_WAIT_REMOTE_DOWNLOAD, base_watcher->states[1].upload);

  std::unique_ptr<RecordingWatcher> base_watcher2 = std::make_unique<RecordingWatcher>();
  aggregator.SetBaseWatcher(base_watcher2.get());

  ASSERT_EQ(1u, base_watcher2->states.size());
  EXPECT_EQ(DOWNLOAD_IN_PROGRESS, base_watcher2->states[0].download);
  EXPECT_EQ(UPLOAD_WAIT_REMOTE_DOWNLOAD, base_watcher2->states[0].upload);

  watcher1->Notify(DOWNLOAD_IDLE, UPLOAD_IDLE);

  ASSERT_EQ(2u, base_watcher2->states.size());
  EXPECT_EQ(DOWNLOAD_IDLE, base_watcher2->states[1].download);
  EXPECT_EQ(UPLOAD_IDLE, base_watcher2->states[1].upload);

  // States in first base watcher have not changed.
  EXPECT_EQ(2u, base_watcher->states.size());
}

}  // namespace

}  // namespace cloud_sync
