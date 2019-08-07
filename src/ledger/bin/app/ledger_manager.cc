// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/ledger_manager.h"

#include <lib/async/cpp/task.h>
#include <lib/callback/ensure_called.h>
#include <lib/callback/scoped_callback.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/inspect_deprecated/inspect.h>

#include <string>
#include <utility>
#include <vector>

#include <trace/event.h>

#include "src/ledger/bin/app/active_page_manager_container.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/page_connection_notifier.h"
#include "src/ledger/bin/app/page_manager.h"
#include "src/ledger/bin/app/page_utils.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/inspect/inspect.h"
#include "src/ledger/bin/p2p_sync/public/page_communicator.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace ledger {

LedgerManager::LedgerManager(Environment* environment, std::string ledger_name,
                             inspect_deprecated::Node inspect_node,
                             std::unique_ptr<encryption::EncryptionService> encryption_service,
                             std::unique_ptr<storage::LedgerStorage> storage,
                             std::unique_ptr<sync_coordinator::LedgerSync> ledger_sync,
                             std::vector<PageUsageListener*> page_usage_listeners)
    : environment_(environment),
      ledger_name_(std::move(ledger_name)),
      encryption_service_(std::move(encryption_service)),
      storage_(std::move(storage)),
      ledger_sync_(std::move(ledger_sync)),
      ledger_impl_(environment_, this),
      merge_manager_(environment_),
      page_usage_listeners_(std::move(page_usage_listeners)),
      inspect_node_(std::move(inspect_node)),
      pages_node_(inspect_node_.CreateChild(kPagesInspectPathComponent.ToString())),
      children_manager_retainer_(pages_node_.SetChildrenManager(this)),
      weak_factory_(this) {
  bindings_.set_on_empty([this] { CheckEmpty(); });
  page_managers_.set_on_empty([this] { CheckEmpty(); });
}

LedgerManager::~LedgerManager() {}

fit::closure LedgerManager::CreateDetacher() {
  outstanding_detachers_++;
  return [this]() {
    outstanding_detachers_--;
    FXL_DCHECK(outstanding_detachers_ >= 0);
    CheckEmpty();
  };
}

void LedgerManager::BindLedger(fidl::InterfaceRequest<Ledger> ledger_request) {
  bindings_.emplace(&ledger_impl_, std::move(ledger_request));
}

void LedgerManager::PageIsClosedAndSynced(
    storage::PageIdView page_id,
    fit::function<void(storage::Status, PagePredicateResult)> callback) {
  GetOrCreatePageManager(page_id)->PageIsClosedAndSynced(std::move(callback));
}

void LedgerManager::PageIsClosedOfflineAndEmpty(
    storage::PageIdView page_id,
    fit::function<void(storage::Status, PagePredicateResult)> callback) {
  GetOrCreatePageManager(page_id)->PageIsClosedOfflineAndEmpty(std::move(callback));
}

void LedgerManager::DeletePageStorage(convert::ExtendedStringView page_id,
                                      fit::function<void(storage::Status)> callback) {
  GetOrCreatePageManager(page_id)->DeletePageStorage(std::move(callback));
}

void LedgerManager::TrySyncClosedPage(convert::ExtendedStringView page_id) {
  GetOrCreatePageManager(page_id)->StartPageSync();
}

void LedgerManager::GetPage(storage::PageIdView page_id, PageState page_state,
                            fidl::InterfaceRequest<Page> page_request,
                            fit::function<void(storage::Status)> callback) {
  GetOrCreatePageManager(page_id)->GetPage(page_state, std::move(page_request),
                                           std::move(callback));
}

void LedgerManager::GetNames(fit::function<void(std::vector<std::string>)> callback) {
  storage_->ListPages(
      [callback = std::move(callback)](storage::Status status, std::set<storage::PageId> page_ids) {
        if (status != storage::Status::OK) {
          FXL_LOG(WARNING) << "Status wasn't OK; rather it was " << status << "!";
        }
        std::vector<std::string> display_names;
        display_names.reserve(page_ids.size());
        for (const auto& page_id : page_ids) {
          display_names.push_back(PageIdToDisplayName(page_id));
        }
        callback(display_names);
      });
}

void LedgerManager::Attach(std::string name, fit::function<void(fit::closure)> callback) {
  storage::PageId page_id;
  if (!PageDisplayNameToPageId(name, &page_id)) {
    FXL_DCHECK(false) << "Page display name \"" << name << "\" not convertable into a PageId!";
    callback([]() {});
    return;
  }
  callback(GetOrCreatePageManager(page_id)->CreateDetacher());
};

PageManager* LedgerManager::GetOrCreatePageManager(convert::ExtendedStringView page_id) {
  auto it = page_managers_.find(page_id);
  if (it != page_managers_.end()) {
    return &it->second;
  }

  auto ret = page_managers_.emplace(
      std::piecewise_construct, std::forward_as_tuple(page_id.ToString()),
      std::forward_as_tuple(environment_, ledger_name_, page_id.ToString(), page_usage_listeners_,
                            storage_.get(), ledger_sync_.get(), &merge_manager_,
                            pages_node_.CreateChild(PageIdToDisplayName(page_id.ToString()))));
  FXL_DCHECK(ret.second);
  return &ret.first->second;
}

void LedgerManager::CheckEmpty() {
  if (on_empty_callback_ && bindings_.size() == 0 && page_managers_.empty() &&
      outstanding_detachers_ == 0) {
    on_empty_callback_();
  }
}

void LedgerManager::SetConflictResolverFactory(
    fidl::InterfaceHandle<ConflictResolverFactory> factory) {
  merge_manager_.AddFactory(std::move(factory));
}

}  // namespace ledger
