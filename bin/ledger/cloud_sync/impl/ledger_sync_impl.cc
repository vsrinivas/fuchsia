// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_sync/impl/ledger_sync_impl.h"

#include "peridot/bin/ledger/backoff/exponential_backoff.h"
#include "peridot/bin/ledger/cloud_provider/impl/page_cloud_handler_impl.h"
#include "peridot/bin/ledger/cloud_provider/impl/paths.h"
#include "peridot/bin/ledger/cloud_sync/impl/page_sync_impl.h"
#include "peridot/bin/ledger/encryption/impl/encryption_service_impl.h"
#include "peridot/bin/ledger/firebase/encoding.h"
#include "peridot/bin/ledger/firebase/firebase_impl.h"
#include "peridot/bin/ledger/gcs/cloud_storage_impl.h"

namespace cloud_sync {

LedgerSyncImpl::LedgerSyncImpl(ledger::Environment* environment,
                               const UserConfig* user_config,
                               fxl::StringView app_id,
                               std::unique_ptr<SyncStateWatcher> watcher)
    : environment_(environment),
      user_config_(user_config),
      app_gcs_prefix_(
          cloud_provider_firebase::GetGcsPrefixForApp(user_config->user_id,
                                                      app_id)),
      app_firebase_path_(
          cloud_provider_firebase::GetFirebasePathForApp(user_config->user_id,
                                                         app_id)),
      app_firebase_(std::make_unique<firebase::FirebaseImpl>(
          environment_->network_service(),
          user_config->server_id,
          app_firebase_path_)),
      user_watcher_(std::move(watcher)),
      aggregator_(user_watcher_.get()) {
  FXL_DCHECK(!user_config->server_id.empty());
}

LedgerSyncImpl::~LedgerSyncImpl() {
  FXL_DCHECK(active_page_syncs_.empty());

  if (on_delete_) {
    on_delete_();
  }
}

std::unique_ptr<PageSyncContext> LedgerSyncImpl::CreatePageContext(
    storage::PageStorage* page_storage,
    fxl::Closure error_callback) {
  FXL_DCHECK(page_storage);

  auto result = std::make_unique<PageSyncContext>();
  result->firebase = std::make_unique<firebase::FirebaseImpl>(
      environment_->network_service(), user_config_->server_id,
      cloud_provider_firebase::GetFirebasePathForPage(app_firebase_path_,
                                                      page_storage->GetId()));
  result->cloud_storage = std::make_unique<gcs::CloudStorageImpl>(
      environment_->main_runner(), environment_->network_service(),
      user_config_->server_id,
      cloud_provider_firebase::GetGcsPrefixForPage(app_gcs_prefix_,
                                                   page_storage->GetId()));
  result->cloud_provider =
      std::make_unique<cloud_provider_firebase::PageCloudHandlerImpl>(
          result->firebase.get(), result->cloud_storage.get());
  // TODO(qsr): LE-330 Review how and where encryption services are created.
  result->encryption_service =
      std::make_unique<encryption::EncryptionServiceImpl>(
          environment_->main_runner());
  auto page_sync = std::make_unique<PageSyncImpl>(
      environment_->main_runner(), page_storage,
      result->encryption_service.get(), result->cloud_provider.get(),
      user_config_->auth_provider,
      std::make_unique<backoff::ExponentialBackoff>(), error_callback,
      aggregator_.GetNewStateWatcher());
  if (upload_enabled_) {
    page_sync->EnableUpload();
  }
  active_page_syncs_.insert(page_sync.get());
  page_sync->set_on_delete([this, page_sync = page_sync.get()]() {
    active_page_syncs_.erase(page_sync);
  });
  result->page_sync = std::move(page_sync);
  return result;
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
