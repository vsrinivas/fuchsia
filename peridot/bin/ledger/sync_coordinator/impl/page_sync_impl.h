// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_SYNC_COORDINATOR_IMPL_PAGE_SYNC_IMPL_H_
#define PERIDOT_BIN_LEDGER_SYNC_COORDINATOR_IMPL_PAGE_SYNC_IMPL_H_

#include <functional>

#include <lib/fit/function.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/ledger/cloud_sync/public/page_sync.h"
#include "peridot/bin/ledger/p2p_sync/public/page_communicator.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/storage/public/page_sync_client.h"
#include "peridot/bin/ledger/storage/public/page_sync_delegate.h"
#include "peridot/bin/ledger/sync_coordinator/impl/sync_watcher_converter.h"
#include "peridot/bin/ledger/sync_coordinator/public/page_sync.h"

namespace sync_coordinator {

class PageSyncImpl : public PageSync, public storage::PageSyncDelegate {
 public:
  PageSyncImpl(storage::PageStorage* storage,
               storage::PageSyncClient* sync_client);
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
  void SetOnIdle(fit::closure on_idle) override;
  bool IsIdle() override;
  void SetOnBacklogDownloaded(fit::closure on_backlog_downloaded) override;
  void SetSyncWatcher(SyncStateWatcher* watcher) override;

  // PageSyncDelegate:
  void GetObject(
      storage::ObjectIdentifier object_identifier,
      fit::function<void(storage::Status status,
                         storage::ChangeSource change_source,
                         storage::IsObjectSynced is_object_synced,
                         std::unique_ptr<storage::DataSource::DataChunk>)>
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

#endif  // PERIDOT_BIN_LEDGER_SYNC_COORDINATOR_IMPL_PAGE_SYNC_IMPL_H_
