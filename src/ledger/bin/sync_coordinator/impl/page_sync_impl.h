// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_SYNC_COORDINATOR_IMPL_PAGE_SYNC_IMPL_H_
#define SRC_LEDGER_BIN_SYNC_COORDINATOR_IMPL_PAGE_SYNC_IMPL_H_

#include <lib/fit/function.h>

#include <functional>

#include "src/ledger/bin/cloud_sync/public/page_sync.h"
#include "src/ledger/bin/p2p_sync/public/page_communicator.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/page_sync_client.h"
#include "src/ledger/bin/storage/public/page_sync_delegate.h"
#include "src/ledger/bin/sync_coordinator/impl/sync_watcher_converter.h"
#include "src/ledger/bin/sync_coordinator/public/page_sync.h"

namespace sync_coordinator {

class PageSyncImpl : public PageSync, public storage::PageSyncDelegate {
 public:
  PageSyncImpl(storage::PageStorage* storage, storage::PageSyncClient* sync_client);
  ~PageSyncImpl() override;

  // Creates a PageSyncClient for cloud synchronization. This method should be
  // called at most once.
  storage::PageSyncClient* CreateCloudSyncClient();
  // Sets the PageSync for cloud synchronization. A Cloud sync client should
  // have been created first.
  void SetCloudSync(std::unique_ptr<cloud_sync::PageSync> cloud_sync);

  // Creates a PageSyncClient for p2p synchronization. This method should be
  // called at most once.
  storage::PageSyncClient* CreateP2PSyncClient();
  // Sets the PageSync for p2p synchronization. A P2P sync client should have
  // been created first.
  void SetP2PSync(std::unique_ptr<p2p_sync::PageCommunicator> p2p_sync);

  // PageSync:
  void Start() override;
  void SetOnPaused(fit::closure on_paused) override;
  bool IsPaused() override;
  void SetOnBacklogDownloaded(fit::closure on_backlog_downloaded) override;
  void SetSyncWatcher(SyncStateWatcher* watcher) override;

  // PageSyncDelegate:
  void GetObject(storage::ObjectIdentifier object_identifier,
                 storage::RetrievedObjectType retrieved_object_type,
                 fit::function<void(ledger::Status, storage::ChangeSource, storage::IsObjectSynced,
                                    std::unique_ptr<storage::DataSource::DataChunk>)>
                     callback) override;
  void GetDiff(
      storage::CommitId commit_id, std::vector<storage::CommitId> possible_bases,
      fit::function<void(ledger::Status, storage::CommitId, std::vector<storage::EntryChange>)>
          callback) override;

 private:
  class CloudSyncHolder;
  class P2PSyncHolder;

  std::unique_ptr<SyncWatcherConverter> watcher_;
  std::unique_ptr<CloudSyncHolder> cloud_sync_;
  std::unique_ptr<P2PSyncHolder> p2p_sync_;

  storage::PageStorage* const storage_;
  storage::PageSyncClient* const sync_client_;
};

}  // namespace sync_coordinator

#endif  // SRC_LEDGER_BIN_SYNC_COORDINATOR_IMPL_PAGE_SYNC_IMPL_H_
