// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_sync/impl/ledger_sync_impl.h"

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/backoff/exponential_backoff.h>
#include <lib/fit/function.h>

#include "peridot/bin/ledger/cloud_sync/impl/page_sync_impl.h"
#include "peridot/bin/ledger/encryption/impl/encryption_service_impl.h"

namespace cloud_sync {

LedgerSyncImpl::LedgerSyncImpl(
    ledger::Environment* environment, const UserConfig* user_config,
    encryption::EncryptionService* encryption_service, fxl::StringView app_id,
    std::unique_ptr<SyncStateWatcher> watcher)
    : environment_(environment),
      user_config_(user_config),
      encryption_service_(encryption_service),
      app_id_(app_id.ToString()),
      user_watcher_(std::move(watcher)) {
  FXL_DCHECK(user_config_->cloud_provider);
  aggregator_.SetBaseWatcher(user_watcher_.get());
}

LedgerSyncImpl::~LedgerSyncImpl() {
  FXL_DCHECK(active_page_syncs_.empty());

  if (on_delete_) {
    on_delete_();
  }
}

std::unique_ptr<PageSync> LedgerSyncImpl::CreatePageSync(
    storage::PageStorage* page_storage,
    storage::PageSyncClient* page_sync_client, fit::closure error_callback) {
  FXL_DCHECK(page_storage);

  cloud_provider::PageCloudPtr page_cloud;
  user_config_->cloud_provider->GetPageCloud(
      convert::ToArray(app_id_), convert::ToArray(page_storage->GetId()),
      page_cloud.NewRequest(), [](auto status) {
        if (status != cloud_provider::Status::OK) {
          FXL_LOG(ERROR) << "Failed to retrieve page cloud, status: " << status;
          // Only log. This should be handled by page cloud connection error
          // handler.
        }
      });
  auto page_sync = std::make_unique<PageSyncImpl>(
      environment_->async(), page_storage, page_sync_client,
      encryption_service_, std::move(page_cloud), environment_->MakeBackoff(),
      environment_->MakeBackoff(), std::move(error_callback),
      aggregator_.GetNewStateWatcher());
  if (upload_enabled_) {
    page_sync->EnableUpload();
  }
  active_page_syncs_.insert(page_sync.get());
  page_sync->set_on_delete([this, page_sync = page_sync.get()]() {
    active_page_syncs_.erase(page_sync);
  });
  return page_sync;
}

void LedgerSyncImpl::EnableUpload() {
  if (upload_enabled_) {
    return;
  }

  upload_enabled_ = true;
  for (auto page_sync : active_page_syncs_) {
    page_sync->EnableUpload();
  }
}

}  // namespace cloud_sync
