// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/ledger_repository_impl.h"

#include <trace/event.h>

#include "apps/ledger/src/cloud_sync/impl/ledger_sync_impl.h"
#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/storage/impl/ledger_storage_impl.h"

namespace ledger {

LedgerRepositoryImpl::LedgerRepositoryImpl(
    std::string base_storage_dir,
    Environment* environment,
    std::unique_ptr<SyncWatcherSet> watchers,
    std::unique_ptr<cloud_sync::UserSync> user_sync)
    : base_storage_dir_(std::move(base_storage_dir)),
      environment_(environment),
      watchers_(std::move(watchers)),
      user_sync_(std::move(user_sync)) {
  bindings_.set_on_empty_set_handler([this] { CheckEmpty(); });
  ledger_managers_.set_on_empty([this] { CheckEmpty(); });
}

LedgerRepositoryImpl::~LedgerRepositoryImpl() {}

void LedgerRepositoryImpl::BindRepository(
    fidl::InterfaceRequest<LedgerRepository> repository_request) {
  bindings_.AddBinding(this, std::move(repository_request));
}

std::vector<fidl::InterfaceRequest<LedgerRepository>>
LedgerRepositoryImpl::Unbind() {
  std::vector<fidl::InterfaceRequest<LedgerRepository>> handles;
  for (auto& binding : bindings_) {
    handles.push_back(binding->Unbind());
  }
  bindings_.CloseAllBindings();
  return handles;
}

void LedgerRepositoryImpl::GetLedger(
    fidl::Array<uint8_t> ledger_name,
    fidl::InterfaceRequest<Ledger> ledger_request,
    const GetLedgerCallback& callback) {
  TRACE_DURATION("ledger", "repository_get_ledger");

  if (ledger_name.empty()) {
    callback(Status::AUTHENTICATION_ERROR);
    return;
  }

  auto it = ledger_managers_.find(ledger_name);
  if (it == ledger_managers_.end()) {
    std::string name_as_string = convert::ToString(ledger_name);
    std::unique_ptr<storage::LedgerStorage> ledger_storage =
        std::make_unique<storage::LedgerStorageImpl>(
            environment_->coroutine_service(), base_storage_dir_,
            name_as_string);
    std::unique_ptr<cloud_sync::LedgerSync> ledger_sync;
    if (user_sync_) {
      ledger_sync = user_sync_->CreateLedgerSync(name_as_string);
    }
    auto result = ledger_managers_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(std::move(name_as_string)),
        std::forward_as_tuple(environment_, std::move(ledger_storage),
                              std::move(ledger_sync)));
    FXL_DCHECK(result.second);
    it = result.first;
  }

  it->second.BindLedger(std::move(ledger_request));
  callback(Status::OK);
}

void LedgerRepositoryImpl::Duplicate(
    fidl::InterfaceRequest<LedgerRepository> request,
    const DuplicateCallback& callback) {
  BindRepository(std::move(request));
  callback(Status::OK);
}

void LedgerRepositoryImpl::SetSyncStateWatcher(
    fidl::InterfaceHandle<SyncWatcher> watcher,
    const SetSyncStateWatcherCallback& callback) {
  watchers_->AddSyncWatcher(std::move(watcher));
  callback(Status::OK);
}

void LedgerRepositoryImpl::CheckEmpty() {
  if (!on_empty_callback_)
    return;
  if (ledger_managers_.empty() && bindings_.size() == 0)
    on_empty_callback_();
}

}  // namespace ledger
