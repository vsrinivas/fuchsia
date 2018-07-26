// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/ledger_repository_impl.h"

#include <trace/event.h>

#include "peridot/bin/ledger/cloud_sync/impl/ledger_sync_impl.h"
#include "peridot/bin/ledger/p2p_sync/public/ledger_communicator.h"
#include "peridot/bin/ledger/storage/impl/ledger_storage_impl.h"
#include "peridot/bin/ledger/sync_coordinator/public/ledger_sync.h"
#include "peridot/lib/convert/convert.h"

namespace ledger {

LedgerRepositoryImpl::LedgerRepositoryImpl(
    DetachedPath content_path, Environment* environment,
    std::unique_ptr<SyncWatcherSet> watchers,
    std::unique_ptr<sync_coordinator::UserSync> user_sync,
    std::unique_ptr<PageEvictionManager> page_eviction_manager)
    : content_path_(std::move(content_path)),
      environment_(environment),
      encryption_service_factory_(environment->dispatcher()),
      watchers_(std::move(watchers)),
      user_sync_(std::move(user_sync)),
      page_eviction_manager_(std::move(page_eviction_manager)) {
  bindings_.set_empty_set_handler([this] { CheckEmpty(); });
  ledger_managers_.set_on_empty([this] { CheckEmpty(); });
  ledger_repository_debug_bindings_.set_empty_set_handler(
      [this] { CheckEmpty(); });
  page_eviction_manager_->set_on_empty([this] { CheckEmpty(); });
}

LedgerRepositoryImpl::~LedgerRepositoryImpl() {}

void LedgerRepositoryImpl::BindRepository(
    fidl::InterfaceRequest<ledger_internal::LedgerRepository>
        repository_request) {
  bindings_.AddBinding(this, std::move(repository_request));
}

void LedgerRepositoryImpl::PageIsClosedAndSynced(
    fxl::StringView ledger_name, storage::PageIdView page_id,
    fit::function<void(Status, PageClosedAndSynced)> callback) {
  LedgerManager* ledger_manager =
      GetLedgerManager(ledger_name, CreateIfMissing::YES);
  FXL_DCHECK(ledger_manager);

  ledger_manager->PageIsClosedAndSynced(page_id, std::move(callback));
}

void LedgerRepositoryImpl::DeletePageStorage(
    fxl::StringView ledger_name, storage::PageIdView page_id,
    fit::function<void(Status)> callback) {
  LedgerManager* ledger_manager =
      GetLedgerManager(ledger_name, CreateIfMissing::YES);
  FXL_DCHECK(ledger_manager);
  return ledger_manager->DeletePageStorage(page_id, std::move(callback));
}

std::vector<fidl::InterfaceRequest<ledger_internal::LedgerRepository>>
LedgerRepositoryImpl::Unbind() {
  std::vector<fidl::InterfaceRequest<ledger_internal::LedgerRepository>>
      handles;
  for (auto& binding : bindings_.bindings()) {
    handles.push_back(binding->Unbind());
  }
  bindings_.CloseAll();
  return handles;
}

LedgerManager* LedgerRepositoryImpl::GetLedgerManager(
    convert::ExtendedStringView ledger_name,
    CreateIfMissing create_if_missing) {
  FXL_DCHECK(!ledger_name.empty());

  // If the Ledger instance is already open return it directly.
  auto it = ledger_managers_.find(ledger_name);
  if (it != ledger_managers_.end()) {
    return &(it->second);
  }

  if (create_if_missing == CreateIfMissing::NO) {
    return nullptr;
  }

  std::string name_as_string = convert::ToString(ledger_name);
  std::unique_ptr<encryption::EncryptionService> encryption_service =
      encryption_service_factory_.MakeEncryptionService(name_as_string);
  auto ledger_storage = std::make_unique<storage::LedgerStorageImpl>(
      environment_, encryption_service.get(), content_path_, name_as_string);
  std::unique_ptr<sync_coordinator::LedgerSync> ledger_sync;
  if (user_sync_) {
    ledger_sync =
        user_sync_->CreateLedgerSync(name_as_string, encryption_service.get());
  }
  auto result = ledger_managers_.emplace(
      std::piecewise_construct, std::forward_as_tuple(name_as_string),
      std::forward_as_tuple(environment_, std::move(name_as_string),
                            std::move(encryption_service),
                            std::move(ledger_storage), std::move(ledger_sync),
                            page_eviction_manager_.get()));
  FXL_DCHECK(result.second);
  return &(result.first->second);
}

void LedgerRepositoryImpl::GetLedger(
    fidl::VectorPtr<uint8_t> ledger_name,
    fidl::InterfaceRequest<Ledger> ledger_request, GetLedgerCallback callback) {
  TRACE_DURATION("ledger", "repository_get_ledger");
  if (ledger_name->empty()) {
    callback(Status::INVALID_ARGUMENT);
    return;
  }

  LedgerManager* ledger_manager =
      GetLedgerManager(ledger_name, CreateIfMissing::YES);
  FXL_DCHECK(ledger_manager);
  ledger_manager->BindLedger(std::move(ledger_request));
  callback(Status::OK);
}

void LedgerRepositoryImpl::Duplicate(
    fidl::InterfaceRequest<ledger_internal::LedgerRepository> request,
    DuplicateCallback callback) {
  BindRepository(std::move(request));
  callback(Status::OK);
}

void LedgerRepositoryImpl::SetSyncStateWatcher(
    fidl::InterfaceHandle<SyncWatcher> watcher,
    SetSyncStateWatcherCallback callback) {
  watchers_->AddSyncWatcher(std::move(watcher));
  callback(Status::OK);
}

void LedgerRepositoryImpl::CheckEmpty() {
  if (!on_empty_callback_)
    return;
  if (ledger_managers_.empty() && bindings_.size() == 0 &&
      ledger_repository_debug_bindings_.size() == 0 &&
      page_eviction_manager_->IsEmpty()) {
    on_empty_callback_();
  }
}

void LedgerRepositoryImpl::GetLedgerRepositoryDebug(
    fidl::InterfaceRequest<ledger_internal::LedgerRepositoryDebug> request,
    GetLedgerRepositoryDebugCallback callback) {
  ledger_repository_debug_bindings_.AddBinding(this, std::move(request));
  callback(Status::OK);
}

void LedgerRepositoryImpl::DiskCleanUp(DiskCleanUpCallback callback) {
  if (clean_up_in_progress_) {
    callback(Status::ILLEGAL_STATE);
    return;
  }
  clean_up_in_progress_ = true;
  page_eviction_manager_->TryCleanUp(
      [this, callback = std::move(callback)](Status status) {
        FXL_DCHECK(clean_up_in_progress_);

        clean_up_in_progress_ = false;
        callback(status);
      });
}

void LedgerRepositoryImpl::GetInstancesList(GetInstancesListCallback callback) {
  fidl::VectorPtr<fidl::VectorPtr<uint8_t>> result =
      fidl::VectorPtr<fidl::VectorPtr<uint8_t>>::New(0);
  for (const auto& key_value : ledger_managers_) {
    result.push_back(convert::ToArray(key_value.first));
  }
  callback(std::move(result));
}

void LedgerRepositoryImpl::GetLedgerDebug(
    fidl::VectorPtr<uint8_t> ledger_name,
    fidl::InterfaceRequest<ledger_internal::LedgerDebug> request,
    GetLedgerDebugCallback callback) {
  auto it = ledger_managers_.find(ledger_name);
  if (it == ledger_managers_.end()) {
    callback(Status::KEY_NOT_FOUND);
  } else {
    it->second.BindLedgerDebug(std::move(request));
    callback(Status::OK);
  }
}

}  // namespace ledger
