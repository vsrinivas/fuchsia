// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/ledger_repository_impl.h"

#include <lib/async/cpp/task.h>
#include <lib/inspect_deprecated/deprecated/expose.h>
#include <lib/inspect_deprecated/deprecated/object_dir.h>
#include <trace/event.h>

#include "peridot/lib/base64url/base64url.h"
#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/page_utils.h"
#include "src/ledger/bin/cloud_sync/impl/ledger_sync_impl.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/filesystem/directory_reader.h"
#include "src/ledger/bin/inspect/inspect.h"
#include "src/ledger/bin/p2p_sync/public/ledger_communicator.h"
#include "src/ledger/bin/storage/impl/ledger_storage_impl.h"
#include "src/ledger/bin/sync_coordinator/public/ledger_sync.h"

namespace ledger {
namespace {
// Encodes opaque bytes in a way that is usable as a directory name.
std::string GetDirectoryName(fxl::StringView bytes) { return base64url::Base64UrlEncode(bytes); }
}  // namespace

LedgerRepositoryImpl::LedgerRepositoryImpl(DetachedPath content_path, Environment* environment,
                                           std::unique_ptr<storage::DbFactory> db_factory,
                                           std::unique_ptr<SyncWatcherSet> watchers,
                                           std::unique_ptr<sync_coordinator::UserSync> user_sync,
                                           std::unique_ptr<DiskCleanupManager> disk_cleanup_manager,
                                           PageUsageListener* page_usage_listener,
                                           inspect_deprecated::Node inspect_node)
    : content_path_(std::move(content_path)),
      environment_(environment),
      db_factory_(std::move(db_factory)),
      encryption_service_factory_(environment),
      watchers_(std::move(watchers)),
      user_sync_(std::move(user_sync)),
      page_usage_listener_(page_usage_listener),
      disk_cleanup_manager_(std::move(disk_cleanup_manager)),
      inspect_node_(std::move(inspect_node)),
      requests_metric_(
          inspect_node_.CreateUIntMetric(kRequestsInspectPathComponent.ToString(), 0UL)),
      ledgers_inspect_node_(inspect_node_.CreateChild(kLedgersInspectPathComponent.ToString())) {
  bindings_.set_on_empty([this] { CheckEmpty(); });
  ledger_managers_.set_on_empty([this] { CheckEmpty(); });
  disk_cleanup_manager_->set_on_empty([this] { CheckEmpty(); });
  children_manager_retainer_ = ledgers_inspect_node_.SetChildrenManager(this);
}

LedgerRepositoryImpl::~LedgerRepositoryImpl() {
  for (auto& binding : bindings_) {
    // |Close()| does not call |binding|'s |on_empty| callback, so |binding| is
    // not destroyed after this call. This would be a memory leak if we were not
    // in |LedgerRepositoryImpl| destructor: as we are in the destructor,
    // |bindings| will be destroyed at the end of this method, and no leak will
    // happen.
    binding.Close(ZX_OK);
  }
}

void LedgerRepositoryImpl::BindRepository(
    fidl::InterfaceRequest<ledger_internal::LedgerRepository> repository_request) {
  bindings_.emplace(this, std::move(repository_request));
  requests_metric_.Add(1);
}

void LedgerRepositoryImpl::PageIsClosedAndSynced(
    fxl::StringView ledger_name, storage::PageIdView page_id,
    fit::function<void(Status, PagePredicateResult)> callback) {
  LedgerManager* ledger_manager;
  Status status = GetLedgerManager(ledger_name, &ledger_manager);
  if (status != Status::OK) {
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
    fit::function<void(Status, PagePredicateResult)> callback) {
  LedgerManager* ledger_manager;
  Status status = GetLedgerManager(ledger_name, &ledger_manager);
  if (status != Status::OK) {
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

void LedgerRepositoryImpl::DeletePageStorage(fxl::StringView ledger_name,
                                             storage::PageIdView page_id,
                                             fit::function<void(Status)> callback) {
  LedgerManager* ledger_manager;
  Status status = GetLedgerManager(ledger_name, &ledger_manager);
  if (status != Status::OK) {
    callback(status);
    return;
  }
  FXL_DCHECK(ledger_manager);
  return ledger_manager->DeletePageStorage(page_id, std::move(callback));
}

// TODO(https://fuchsia.atlassian.net/browse/LE-792): The disk scan should be
// made to happen either asynchronously or not on the main thread.
void LedgerRepositoryImpl::GetNames(fit::function<void(std::vector<std::string>)> callback) {
  std::vector<std::string> child_names;
  ledger::GetDirectoryEntries(content_path_, [&child_names](fxl::StringView entry) {
    std::string decoded;
    if (base64url::Base64UrlDecode(entry, &decoded)) {
      child_names.push_back(decoded);
      return true;
    } else {
      // NOTE(nathaniel): The ChildrenManager API does not currently have a
      // means to indicate errors; our response to an error here is to
      // simply log and refrain from telling Inspect that the problematic
      // child exists.
      FXL_LOG(ERROR) << "Failed to decode encoded ledger name \"" << entry << "\"!";
      return false;
    }
  });
  callback(child_names);
};

void LedgerRepositoryImpl::Attach(std::string ledger_name,
                                  fit::function<void(fit::closure)> callback) {
  LedgerManager* ledger_manager;
  // TODO(https://fuchsia.atlassian.net/browse/LE-793): This will create a new
  // ledger on disk if no ledger with the given name is found - GetLedgerManager
  // should be split into separate "GetOrCreateLedgerManager" and
  // "GetButDoNotCreateLedgerManager" functions with the latter called here.
  storage::Status status = GetLedgerManager(ledger_name, &ledger_manager);
  if (status != storage::Status::OK) {
    callback([]() {});
    return;
  }
  callback(ledger_manager->CreateDetacher());
};

std::vector<fidl::InterfaceRequest<ledger_internal::LedgerRepository>>
LedgerRepositoryImpl::Unbind() {
  std::vector<fidl::InterfaceRequest<ledger_internal::LedgerRepository>> handles;
  for (auto& binding : bindings_) {
    handles.push_back(binding.Unbind());
  }
  bindings_.clear();
  return handles;
}

Status LedgerRepositoryImpl::GetLedgerManager(convert::ExtendedStringView ledger_name,
                                              LedgerManager** ledger_manager) {
  FXL_DCHECK(!ledger_name.empty());

  // If the Ledger instance is already open return it directly.
  auto it = ledger_managers_.find(ledger_name);
  if (it != ledger_managers_.end()) {
    *ledger_manager = &(it->second);
    return Status::OK;
  }

  std::string name_as_string = convert::ToString(ledger_name);
  std::unique_ptr<encryption::EncryptionService> encryption_service =
      encryption_service_factory_.MakeEncryptionService(name_as_string);
  std::unique_ptr<sync_coordinator::LedgerSync> ledger_sync;
  storage::CommitPruningPolicy pruning_policy;
  if (user_sync_) {
    ledger_sync = user_sync_->CreateLedgerSync(name_as_string, encryption_service.get());
    pruning_policy = storage::CommitPruningPolicy::NEVER;
  } else {
    pruning_policy = storage::CommitPruningPolicy::LOCAL_IMMEDIATE;
  }
  auto ledger_storage = std::make_unique<storage::LedgerStorageImpl>(
      environment_, encryption_service.get(), db_factory_.get(), GetPathFor(name_as_string),
      pruning_policy);
  Status status = ledger_storage->Init();
  if (status != Status::OK) {
    return status;
  }
  auto result = ledger_managers_.emplace(
      std::piecewise_construct, std::forward_as_tuple(name_as_string),
      std::forward_as_tuple(environment_, name_as_string,
                            ledgers_inspect_node_.CreateChild(name_as_string),
                            std::move(encryption_service), std::move(ledger_storage),
                            std::move(ledger_sync), page_usage_listener_));
  FXL_DCHECK(result.second);
  *ledger_manager = &(result.first->second);
  return Status::OK;
}

void LedgerRepositoryImpl::GetLedger(std::vector<uint8_t> ledger_name,
                                     fidl::InterfaceRequest<Ledger> ledger_request,
                                     fit::function<void(Status)> callback) {
  TRACE_DURATION("ledger", "repository_get_ledger");

  if (close_callback_) {
    // Attempting to call a method on LedgerRepository while closing it is
    // illegal.
    callback(Status::ILLEGAL_STATE);
    return;
  }

  if (ledger_name.empty()) {
    callback(Status::INVALID_ARGUMENT);
    return;
  }

  LedgerManager* ledger_manager;
  Status status = GetLedgerManager(ledger_name, &ledger_manager);
  if (status != Status::OK) {
    callback(status);
    return;
  }
  FXL_DCHECK(ledger_manager);
  ledger_manager->BindLedger(std::move(ledger_request));
  callback(Status::OK);
}

void LedgerRepositoryImpl::Duplicate(
    fidl::InterfaceRequest<ledger_internal::LedgerRepository> request,
    fit::function<void(Status)> callback) {
  if (close_callback_) {
    // Attempting to call a method on LedgerRepository while closing it is
    // illegal.
    callback(Status::ILLEGAL_STATE);
    return;
  }

  BindRepository(std::move(request));
  callback(Status::OK);
}

void LedgerRepositoryImpl::SetSyncStateWatcher(fidl::InterfaceHandle<SyncWatcher> watcher,
                                               fit::function<void(Status)> callback) {
  if (close_callback_) {
    // Attempting to call a method on LedgerRepository while closing it is
    // illegal.
    callback(Status::ILLEGAL_STATE);
    return;
  }

  watchers_->AddSyncWatcher(std::move(watcher));
  callback(Status::OK);
}

void LedgerRepositoryImpl::CheckEmpty() {
  if (ledger_managers_.empty() && (bindings_.empty() || close_callback_) &&
      disk_cleanup_manager_->IsEmpty()) {
    if (close_callback_) {
      close_callback_(Status::OK);
    }
    if (on_empty_callback_) {
      on_empty_callback_();
    }
  }
}

void LedgerRepositoryImpl::DiskCleanUp(fit::function<void(Status)> callback) {
  if (close_callback_) {
    // Attempting to call a method on LedgerRepository while closing it is
    // illegal.
    callback(Status::ILLEGAL_STATE);
    return;
  }

  cleanup_callbacks_.push_back(std::move(callback));
  if (cleanup_callbacks_.size() > 1) {
    return;
  }
  disk_cleanup_manager_->TryCleanUp([this](Status status) {
    FXL_DCHECK(!cleanup_callbacks_.empty());

    auto callbacks = std::move(cleanup_callbacks_);
    cleanup_callbacks_.clear();
    for (auto& callback : callbacks) {
      callback(status);
    }
  });
}

DetachedPath LedgerRepositoryImpl::GetPathFor(fxl::StringView ledger_name) {
  FXL_DCHECK(!ledger_name.empty());
  return content_path_.SubPath(GetDirectoryName(ledger_name));
}

void LedgerRepositoryImpl::Close(fit::function<void(Status)> callback) {
  if (close_callback_) {
    // Closing the repository twice is force-terminating it.
    callback(Status::ILLEGAL_STATE);
    return;
  }
  close_callback_ = std::move(callback);
}

}  // namespace ledger
