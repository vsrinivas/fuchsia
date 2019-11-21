// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/sync_coordinator/impl/ledger_sync_impl.h"

#include <lib/fit/function.h>

#include <src/ledger/lib/convert/convert.h>

#include "src/ledger/bin/sync_coordinator/impl/page_sync_impl.h"

namespace sync_coordinator {

LedgerSyncImpl::LedgerSyncImpl(std::unique_ptr<cloud_sync::LedgerSync> cloud_sync,
                               std::unique_ptr<p2p_sync::LedgerCommunicator> p2p_sync)
    : cloud_sync_(std::move(cloud_sync)), p2p_sync_(std::move(p2p_sync)) {
  FXL_DCHECK(cloud_sync_ || p2p_sync_);
}

LedgerSyncImpl::~LedgerSyncImpl() = default;

void LedgerSyncImpl::CreatePageSync(
    storage::PageStorage* page_storage, storage::PageSyncClient* page_sync_client,
    fit::function<void(storage::Status, std::unique_ptr<PageSync>)> callback) {
  auto combined_sync = std::make_unique<PageSyncImpl>(page_storage, page_sync_client);

  if (p2p_sync_) {
    auto p2p_page_sync =
        p2p_sync_->GetPageCommunicator(page_storage, combined_sync->CreateP2PSyncClient());
    combined_sync->SetP2PSync(std::move(p2p_page_sync));
  }

  if (!cloud_sync_) {
    callback(storage::Status::OK, std::move(combined_sync));
    return;
  }
  cloud_sync_->CreatePageSync(
      page_storage, combined_sync->CreateCloudSyncClient(),
      [combined_sync = std::move(combined_sync), page_storage, callback = std::move(callback)](
          storage::Status status, std::unique_ptr<cloud_sync::PageSync> cloud_page_sync) mutable {
        if (status != storage::Status::OK) {
          // Only print a warning there, cloud errors should be handled in cloud_sync.
          FXL_LOG(WARNING) << "cloud_sync set, but failed to get a PageSync for the page "
                           << convert::ToHex(page_storage->GetId());
        }
        combined_sync->SetCloudSync(std::move(cloud_page_sync));
        callback(storage::Status::OK, std::move(combined_sync));
      });
}

}  // namespace sync_coordinator
