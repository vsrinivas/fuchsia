// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/ledger_repository_impl.h"

#include <lib/inspect/deprecated/expose.h>
#include <lib/inspect/deprecated/object_dir.h>
#include <trace/event.h>

#include "peridot/lib/base64url/base64url.h"
#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/page_utils.h"
#include "src/ledger/bin/cloud_sync/impl/ledger_sync_impl.h"
#include "src/ledger/bin/p2p_sync/public/ledger_communicator.h"
#include "src/ledger/bin/storage/impl/ledger_storage_impl.h"
#include "src/ledger/bin/sync_coordinator/public/ledger_sync.h"

namespace ledger {
namespace {
// Encodes opaque bytes in a way that is usable as a directory name.
std::string GetDirectoryName(fxl::StringView bytes) {
  return base64url::Base64UrlEncode(bytes);
}
}  // namespace

LedgerRepositoryImpl::LedgerRepositoryImpl(
    DetachedPath content_path, Environment* environment,
    std::unique_ptr<storage::DbFactory> db_factory,
    std::unique_ptr<SyncWatcherSet> watchers,
    std::unique_ptr<sync_coordinator::UserSync> user_sync,
    std::unique_ptr<DiskCleanupManager> disk_cleanup_manager,
    PageUsageListener* page_usage_listener, inspect::Object inspect_object)
    : content_path_(std::move(content_path)),
      environment_(environment),
      db_factory_(std::move(db_factory)),
      encryption_service_factory_(environment),
      watchers_(std::move(watchers)),
      user_sync_(std::move(user_sync)),
      page_usage_listener_(page_usage_listener),
      disk_cleanup_manager_(std::move(disk_cleanup_manager)),
      inspect_object_(std::move(inspect_object)),
      requests_metric_(
          inspect_object_.CreateUIntMetric(kRequestsInspectPathComponent, 0UL)),
      ledgers_inspect_object_(
          inspect_object_.CreateChild(kLedgersInspectPathComponent)) {
  bindings_.set_on_empty([this] { CheckEmpty(); });
  ledger_managers_.set_on_empty([this] { CheckEmpty(); });
  disk_cleanup_manager_->set_on_empty([this] { CheckEmpty(); });
}

LedgerRepositoryImpl::~LedgerRepositoryImpl() {}

void LedgerRepositoryImpl::BindRepository(
    fidl::InterfaceRequest<ledger_internal::LedgerRepository>
        repository_request) {
  bindings_.emplace(this, std::move(repository_request));
  requests_metric_.Add(1);
}

void LedgerRepositoryImpl::PageIsClosedAndSynced(
    fxl::StringView ledger_name, storage::PageIdView page_id,
    fit::function<void(storage::Status, PagePredicateResult)> callback) {
  LedgerManager* ledger_manager;
  storage::Status status = GetLedgerManager(ledger_name, &ledger_manager);
  if (status != storage::Status::OK) {
    callback(status, PagePredicateResult::PAGE_OPENED);
    return;
  }

  FXL_DCHECK(ledger_manager);
  // |ledger_manager| can be destructed if empty, or if the
  // |LedgerRepositoryImpl| is destructed. In the second case, the callback
  // should not be called. The first case will not happen before the callback
  // has been called, because the manager is non-empty while a page is tracked.
  ledger_manager->PageIsClosedAndSynced(page_id, std::move(callback));
}

void LedgerRepositoryImpl::PageIsClosedOfflineAndEmpty(
    fxl::StringView ledger_name, storage::PageIdView page_id,
    fit::function<void(storage::Status, PagePredicateResult)> callback) {
  LedgerManager* ledger_manager;
  storage::Status status = GetLedgerManager(ledger_name, &ledger_manager);
  if (status != storage::Status::OK) {
    callback(status, PagePredicateResult::PAGE_OPENED);
    return;
  }
  FXL_DCHECK(ledger_manager);
  // |ledger_manager| can be destructed if empty, or if the
  // |LedgerRepositoryImpl| is destructed. In the second case, the callback
  // should not be called. The first case will not happen before the callback
  // has been called, because the manager is non-empty while a page is tracked.
  ledger_manager->PageIsClosedOfflineAndEmpty(page_id, std::move(callback));
}

void LedgerRepositoryImpl::DeletePageStorage(
    fxl::StringView ledger_name, storage::PageIdView page_id,
    fit::function<void(storage::Status)> callback) {
  LedgerManager* ledger_manager;
  storage::Status status = GetLedgerManager(ledger_name, &ledger_manager);
  if (status != storage::Status::OK) {
    callback(status);
    return;
  }
  FXL_DCHECK(ledger_manager);
  return ledger_manager->DeletePageStorage(page_id, std::move(callback));
}

std::vector<fidl::InterfaceRequest<ledger_internal::LedgerRepository>>
LedgerRepositoryImpl::Unbind() {
  std::vector<fidl::InterfaceRequest<ledger_internal::LedgerRepository>>
      handles;
  for (auto& binding : bindings_) {
    handles.push_back(binding.Unbind());
  }
  bindings_.clear();
  return handles;
}

storage::Status LedgerRepositoryImpl::GetLedgerManager(
    convert::ExtendedStringView ledger_name, LedgerManager** ledger_manager) {
  FXL_DCHECK(!ledger_name.empty());

  // If the Ledger instance is already open return it directly.
  auto it = ledger_managers_.find(ledger_name);
  if (it != ledger_managers_.end()) {
    *ledger_manager = &(it->second);
    return storage::Status::OK;
  }

  std::string name_as_string = convert::ToString(ledger_name);
  std::unique_ptr<encryption::EncryptionService> encryption_service =
      encryption_service_factory_.MakeEncryptionService(name_as_string);
  auto ledger_storage = std::make_unique<storage::LedgerStorageImpl>(
      environment_, encryption_service.get(), db_factory_.get(),
      GetPathFor(name_as_string));
  storage::Status status = ledger_storage->Init();
  if (status != storage::Status::OK) {
    return status;
  }
  std::unique_ptr<sync_coordinator::LedgerSync> ledger_sync;
  if (user_sync_) {
    ledger_sync =
        user_sync_->CreateLedgerSync(name_as_string, encryption_service.get());
  }
  auto result = ledger_managers_.emplace(
      std::piecewise_construct, std::forward_as_tuple(name_as_string),
      std::forward_as_tuple(environment_, name_as_string,
                            std::move(encryption_service),
                            std::move(ledger_storage), std::move(ledger_sync),
                            page_usage_listener_));
  FXL_DCHECK(result.second);
  *ledger_manager = &(result.first->second);
  return storage::Status::OK;
}

void LedgerRepositoryImpl::GetLedger(
    std::vector<uint8_t> ledger_name,
    fidl::InterfaceRequest<Ledger> ledger_request,
    fit::function<void(Status)> callback) {
  TRACE_DURATION("ledger", "repository_get_ledger");
  if (ledger_name.empty()) {
    callback(Status::INVALID_ARGUMENT);
    return;
  }

  LedgerManager* ledger_manager;
  storage::Status status = GetLedgerManager(ledger_name, &ledger_manager);
  if (status != storage::Status::OK) {
    callback(PageUtils::ConvertStatus(status));
    return;
  }
  FXL_DCHECK(ledger_manager);
  ledger_manager->BindLedger(std::move(ledger_request));
  callback(Status::OK);
}

void LedgerRepositoryImpl::Duplicate(
    fidl::InterfaceRequest<ledger_internal::LedgerRepository> request,
    fit::function<void(Status)> callback) {
  BindRepository(std::move(request));
  callback(Status::OK);
}

void LedgerRepositoryImpl::SetSyncStateWatcher(
    fidl::InterfaceHandle<SyncWatcher> watcher,
    fit::function<void(Status)> callback) {
  watchers_->AddSyncWatcher(std::move(watcher));
  callback(Status::OK);
}

void LedgerRepositoryImpl::CheckEmpty() {
  if (!on_empty_callback_)
    return;
  if (ledger_managers_.empty() && bindings_.empty() &&
      disk_cleanup_manager_->IsEmpty()) {
    on_empty_callback_();
  }
}

void LedgerRepositoryImpl::DiskCleanUp(fit::function<void(Status)> callback) {
  cleanup_callbacks_.push_back(std::move(callback));
  if (cleanup_callbacks_.size() > 1) {
    return;
  }
  disk_cleanup_manager_->TryCleanUp([this](storage::Status status) {
    FXL_DCHECK(!cleanup_callbacks_.empty());

    auto callbacks = std::move(cleanup_callbacks_);
    cleanup_callbacks_.clear();
    Status ledger_status = PageUtils::ConvertStatus(status);
    for (auto& callback : callbacks) {
      callback(ledger_status);
    }
  });
}

DetachedPath LedgerRepositoryImpl::GetPathFor(fxl::StringView ledger_name) {
  FXL_DCHECK(!ledger_name.empty());
  return content_path_.SubPath(GetDirectoryName(ledger_name));
}

}  // namespace ledger
