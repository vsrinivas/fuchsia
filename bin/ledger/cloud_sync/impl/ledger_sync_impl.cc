// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/impl/ledger_sync_impl.h"

#include "apps/ledger/src/backoff/exponential_backoff.h"
#include "apps/ledger/src/cloud_provider/impl/cloud_provider_impl.h"
#include "apps/ledger/src/cloud_sync/impl/page_sync_impl.h"
#include "apps/ledger/src/firebase/encoding.h"
#include "apps/ledger/src/firebase/firebase_impl.h"
#include "lib/ftl/strings/concatenate.h"

namespace cloud_sync {

std::string GetFirebasePrefix(ftl::StringView user_prefix,
                              ftl::StringView app_id,
                              ftl::StringView page_id) {
  return ftl::Concatenate({firebase::EncodeKey(user_prefix), "/",
                           firebase::EncodeKey(app_id), "/",
                           firebase::EncodeKey(page_id)});
}

LedgerSyncImpl::LedgerSyncImpl(ftl::RefPtr<ftl::TaskRunner> task_runner,
                               ledger::Environment* environment,
                               ftl::StringView app_id)
    : task_runner_(task_runner),
      environment_(environment),
      app_id_(app_id.ToString()) {}

LedgerSyncImpl::~LedgerSyncImpl() {}

std::unique_ptr<PageSyncContext> LedgerSyncImpl::CreatePageContext(
    storage::PageStorage* page_storage,
    std::function<void()> error_callback) {
  FTL_DCHECK(environment_->configuration.use_sync);
  FTL_DCHECK(!environment_->configuration.sync_params.firebase_id.empty());
  FTL_DCHECK(page_storage);

  auto result = std::make_unique<PageSyncContext>();
  result->firebase = std::make_unique<firebase::FirebaseImpl>(
      environment_->network_service,
      environment_->configuration.sync_params.firebase_id,
      GetFirebasePrefix(environment_->configuration.sync_params.firebase_prefix,
                        app_id_, page_storage->GetId()));
  result->cloud_provider = std::make_unique<cloud_provider::CloudProviderImpl>(
      result->firebase.get());
  result->page_sync = std::make_unique<PageSyncImpl>(
      task_runner_, page_storage, result->cloud_provider.get(),
      std::make_unique<backoff::ExponentialBackoff>(), error_callback);
  return result;
}

}  // namespace cloud_sync
