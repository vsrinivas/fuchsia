// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/sync_coordinator/impl/ledger_sync_impl.h"

#include <lib/fit/function.h>

#include "peridot/bin/ledger/sync_coordinator/impl/page_sync_impl.h"

namespace sync_coordinator {

LedgerSyncImpl::LedgerSyncImpl(
    std::unique_ptr<cloud_sync::LedgerSync> cloud_sync,
    std::unique_ptr<p2p_sync::LedgerCommunicator> p2p_sync)
    : cloud_sync_(std::move(cloud_sync)), p2p_sync_(std::move(p2p_sync)) {}

LedgerSyncImpl::~LedgerSyncImpl() {}

std::unique_ptr<PageSync> LedgerSyncImpl::CreatePageSync(
    storage::PageStorage* page_storage,
    storage::PageSyncClient* page_sync_client, fit::closure error_callback) {
  auto combined_sync =
      std::make_unique<PageSyncImpl>(page_storage, page_sync_client);

  if (cloud_sync_) {
    auto cloud_page_sync = cloud_sync_->CreatePageSync(
        page_storage, combined_sync->CreateCloudSyncClient(),
        std::move(error_callback));
    combined_sync->SetCloudSync(std::move(cloud_page_sync));
  }

  if (p2p_sync_) {
    auto p2p_page_sync = p2p_sync_->GetPageCommunicator(
        page_storage, combined_sync->CreateP2PSyncClient());
    combined_sync->SetP2PSync(std::move(p2p_page_sync));
  }

  return combined_sync;
}

}  // namespace sync_coordinator
