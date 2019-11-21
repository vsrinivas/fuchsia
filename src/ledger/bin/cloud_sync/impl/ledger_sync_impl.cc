// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cloud_sync/impl/ledger_sync_impl.h"

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/fit/function.h>

#include "src/ledger/bin/cloud_sync/impl/page_sync_impl.h"
#include "src/ledger/bin/encryption/impl/encryption_service_impl.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/backoff/exponential_backoff.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace cloud_sync {

LedgerSyncImpl::LedgerSyncImpl(ledger::Environment* environment, const UserConfig* user_config,
                               encryption::EncryptionService* encryption_service,
                               absl::string_view app_id, std::unique_ptr<SyncStateWatcher> watcher)
    : environment_(environment),
      user_config_(user_config),
      encryption_service_(encryption_service),
      app_id_(convert::ToString(app_id)),
      user_watcher_(std::move(watcher)) {
  if (!user_config_->cloud_provider) {
    FXL_LOG(ERROR) << "Instantiated a LedgerSyncImpl with an invalid cloud provider.";
  }
  aggregator_.SetBaseWatcher(user_watcher_.get());
}

LedgerSyncImpl::~LedgerSyncImpl() {
  FXL_DCHECK(active_page_syncs_.empty());

  if (on_delete_) {
    on_delete_();
  }
}

void LedgerSyncImpl::CreatePageSync(
    storage::PageStorage* page_storage, storage::PageSyncClient* page_sync_client,
    fit::function<void(storage::Status, std::unique_ptr<PageSync>)> callback) {
  FXL_DCHECK(page_storage);
  if (!user_config_->cloud_provider) {
    // TODO(LE-567): handle recovery from cloud provider disconnection.
    FXL_LOG(WARNING) << "Skipped initializing the cloud sync. "
                     << "Cloud provider is disconnected.";
    callback(storage::Status::INTERNAL_ERROR, nullptr);
    return;
  }

  encryption_service_->GetPageId(page_storage->GetId(), [this, page_storage, page_sync_client,
                                                         callback = std::move(callback)](
                                                            encryption::Status status,
                                                            std::string page_id) {
    if (status != encryption::Status::OK) {
      FXL_LOG(ERROR) << "Failed to get the encoded version of page_id from the encryption service.";
      callback(storage::Status::INTERNAL_ERROR, nullptr);
      return;
    }
    cloud_provider::PageCloudPtr page_cloud;
    user_config_->cloud_provider->GetPageCloud(convert::ToArray(app_id_), convert::ToArray(page_id),
                                               page_cloud.NewRequest(), [](auto status) {
                                                 if (status != cloud_provider::Status::OK) {
                                                   FXL_LOG(ERROR)
                                                       << "Failed to retrieve page cloud, status: "
                                                       << fidl::ToUnderlying(status);
                                                   // Only log. This should be handled by page cloud
                                                   // connection error handler.
                                                 }
                                               });
    auto page_sync = std::make_unique<PageSyncImpl>(
        environment_->dispatcher(), environment_->coroutine_service(), page_storage,
        page_sync_client, encryption_service_, std::move(page_cloud), environment_->MakeBackoff(),
        environment_->MakeBackoff(), aggregator_.GetNewStateWatcher());
    if (upload_enabled_) {
      page_sync->EnableUpload();
    }
    active_page_syncs_.insert(page_sync.get());
    page_sync->set_on_delete(
        [this, page_sync = page_sync.get()]() { active_page_syncs_.erase(page_sync); });
    callback(storage::Status::OK, std::move(page_sync));
  });
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
