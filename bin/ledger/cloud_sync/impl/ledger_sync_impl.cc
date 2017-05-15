// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/impl/ledger_sync_impl.h"

#include "apps/ledger/src/backoff/exponential_backoff.h"
#include "apps/ledger/src/cloud_provider/impl/cloud_provider_impl.h"
#include "apps/ledger/src/cloud_sync/impl/page_sync_impl.h"
#include "apps/ledger/src/cloud_sync/impl/paths.h"
#include "apps/ledger/src/firebase/encoding.h"
#include "apps/ledger/src/firebase/firebase_impl.h"
#include "apps/ledger/src/gcs/cloud_storage_impl.h"

namespace cloud_sync {

LedgerSyncImpl::LedgerSyncImpl(ledger::Environment* environment,
                               const UserConfig* user_config,
                               ftl::StringView app_id)
    : environment_(environment),
      user_config_(user_config),
      app_gcs_prefix_(GetGcsPrefixForApp(user_config->user_id, app_id)),
      app_firebase_path_(GetFirebasePathForApp(user_config->user_id, app_id)),
      app_firebase_(std::make_unique<firebase::FirebaseImpl>(
          environment_->network_service(),
          user_config->server_id,
          app_firebase_path_)) {
  FTL_DCHECK(user_config->use_sync);
  FTL_DCHECK(!user_config->server_id.empty());
}

LedgerSyncImpl::~LedgerSyncImpl() {
  FTL_DCHECK(active_page_syncs_.empty());

  if (on_delete_) {
    on_delete_();
  }
}

void LedgerSyncImpl::RemoteContains(
    ftl::StringView page_id,
    std::function<void(RemoteResponse)> callback) {
  app_firebase_->Get(
      firebase::EncodeKey(page_id), "shallow=true",
      [callback = std::move(callback)](firebase::Status status,
                                       const rapidjson::Value& value) {
        if (status != firebase::Status::OK) {
          FTL_LOG(WARNING) << "Failed to look up the page in Firebase, error: "
                           << status;
          switch (status) {
            case firebase::Status::NETWORK_ERROR:
              callback(RemoteResponse::NETWORK_ERROR);
              return;
            case firebase::Status::PARSE_ERROR:
              callback(RemoteResponse::PARSE_ERROR);
              return;
            case firebase::Status::SERVER_ERROR:
              callback(RemoteResponse::SERVER_ERROR);
              return;
            default:
              FTL_NOTREACHED();
          }
          return;
        }

        callback(value.IsNull() ? RemoteResponse::NOT_FOUND
                                : RemoteResponse::FOUND);
      });
}

std::unique_ptr<PageSyncContext> LedgerSyncImpl::CreatePageContext(
    storage::PageStorage* page_storage,
    ftl::Closure error_callback) {
  FTL_DCHECK(page_storage);

  auto result = std::make_unique<PageSyncContext>();
  result->firebase = std::make_unique<firebase::FirebaseImpl>(
      environment_->network_service(), user_config_->server_id,
      GetFirebasePathForPage(app_firebase_path_, page_storage->GetId()));
  result->cloud_storage = std::make_unique<gcs::CloudStorageImpl>(
      environment_->main_runner(), environment_->network_service(),
      user_config_->server_id,
      GetGcsPrefixForPage(app_gcs_prefix_, page_storage->GetId()));
  result->cloud_provider = std::make_unique<cloud_provider::CloudProviderImpl>(
      result->firebase.get(), result->cloud_storage.get());
  auto page_sync = std::make_unique<PageSyncImpl>(
      environment_->main_runner(), page_storage, result->cloud_provider.get(),
      std::make_unique<backoff::ExponentialBackoff>(), error_callback);
  active_page_syncs_.insert(page_sync.get());
  page_sync->set_on_delete([ this, page_sync = page_sync.get() ]() {
    active_page_syncs_.erase(page_sync);
  });
  result->page_sync = std::move(page_sync);
  return result;
}

}  // namespace cloud_sync
