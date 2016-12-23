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
                               ftl::StringView app_id)
    : environment_(environment),
      app_gcs_prefix_(GetGcsPrefixForApp(
          environment_->configuration().sync_params.user_prefix,
          app_id)),
      app_firebase_path_(GetFirebasePathForApp(
          environment_->configuration().sync_params.user_prefix,
          app_id)),
      app_firebase_(std::make_unique<firebase::FirebaseImpl>(
          environment_->network_service(),
          environment_->configuration().sync_params.firebase_id,
          app_firebase_path_)) {}

LedgerSyncImpl::~LedgerSyncImpl() {}

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
  FTL_DCHECK(environment_->configuration().use_sync);
  FTL_DCHECK(!environment_->configuration().sync_params.firebase_id.empty());
  FTL_DCHECK(page_storage);

  auto result = std::make_unique<PageSyncContext>();
  result->firebase = std::make_unique<firebase::FirebaseImpl>(
      environment_->network_service(),
      environment_->configuration().sync_params.firebase_id,
      GetFirebasePathForPage(app_firebase_path_, page_storage->GetId()));
  result->cloud_storage = std::make_unique<gcs::CloudStorageImpl>(
      environment_->main_runner(), environment_->network_service(),
      environment_->configuration().sync_params.gcs_bucket,
      GetGcsPrefixForPage(app_gcs_prefix_, page_storage->GetId()));
  result->cloud_provider = std::make_unique<cloud_provider::CloudProviderImpl>(
      result->firebase.get(), result->cloud_storage.get());
  result->page_sync = std::make_unique<PageSyncImpl>(
      environment_->main_runner(), page_storage, result->cloud_provider.get(),
      std::make_unique<backoff::ExponentialBackoff>(), error_callback);
  return result;
}

}  // namespace cloud_sync
