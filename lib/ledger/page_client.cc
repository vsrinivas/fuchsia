// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/ledger/page_client.h"

#include <utility>

#include <memory>

#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/ledger/ledger_client.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fsl/vmo/strings.h"

namespace modular {

PageClient::PageClient(std::string context,
                       LedgerClient* ledger_client,
                       LedgerPageId page_id,
                       const char* const prefix)
    : binding_(this),
      context_(std::move(context)),
      ledger_client_(ledger_client),
      page_id_(std::move(page_id)),
      page_(ledger_client_->GetPage(this, context_, page_id_)),
      prefix_(prefix == nullptr ? "" : prefix) {
  page_->GetSnapshot(
      NewRequest(), to_array(prefix_),
      binding_.NewBinding(), [this](ledger::Status status) {
        if (status != ledger::Status::OK) {
          FXL_LOG(ERROR) << context_ << " Page.GetSnapshot() " << status;
        }
      });
}

PageClient::~PageClient() {
  // We assume ledger client always outlives page client.
  ledger_client_->DropPageClient(this);
}

fidl::InterfaceRequest<ledger::PageSnapshot> PageClient::NewRequest() {
  page_snapshot_ = std::make_shared<ledger::PageSnapshotPtr>();
  auto ret = (*page_snapshot_).NewRequest();
  (*page_snapshot_).set_connection_error_handler([this] {
    FXL_LOG(ERROR) << context_ << ": "
                   << "PageSnapshot connection unexpectedly closed.";
  });
  return ret;
}

fidl::InterfaceRequest<ledger::PageSnapshot> PageClient::Update(
    const ledger::ResultState result_state) {
  switch (result_state) {
    case ledger::ResultState::PARTIAL_CONTINUED:
    case ledger::ResultState::PARTIAL_STARTED:
      return nullptr;

    case ledger::ResultState::COMPLETED:
    case ledger::ResultState::PARTIAL_COMPLETED:
      return NewRequest();
  }
}

// |PageWatcher|
void PageClient::OnChange(ledger::PageChangePtr page,
                          ledger::ResultState result_state,
                          const OnChangeCallback& callback) {
  // According to their fidl spec, neither page nor page->changes
  // should be null.
  FXL_DCHECK(page && page->changes);
  for (auto& entry : page->changes) {
    // Remove prefix maybe?
    const std::string key = to_string(entry->key);
    std::string value;
    if (!fsl::StringFromVmo(entry->value, &value)) {
      FXL_LOG(ERROR) << "PageClient::OnChange() " << context_ << ": "
                     << "Unable to extract data.";
      continue;
    }

    OnPageChange(key, value);
  }

  for (auto& key : page->deleted_keys) {
    OnPageDelete(to_string(key));
  }

  // Every time we receive a group of OnChange notifications, we update the root
  // page snapshot so we see the current state. Note that pending Operation
  // instances may hold on to the previous value until they finish. New
  // Operation instances created after the update receive the new snapshot.
  //
  // For continued updates, we only request the snapshot once, in the last
  // OnChange() notification.
  callback(Update(result_state));
}

void PageClient::OnPageChange(const std::string& /*key*/,
                              const std::string& /*value*/) {}

void PageClient::OnPageDelete(const std::string& /*key*/) {}

void PageClient::OnPageConflict(Conflict* const conflict) {
  FXL_LOG(INFO) << "PageClient::OnPageConflict() " << context_
                << " " << conflict->key
                << " " << conflict->left
                << " " << conflict->right;
};

namespace {

void GetEntriesRecursive(ledger::PageSnapshot* const snapshot,
                         std::vector<ledger::EntryPtr>* const entries,
                         LedgerPageKey next_token,
                         std::function<void(ledger::Status)> callback) {
  snapshot->GetEntries(
      nullptr /* key_start */, std::move(next_token),
      fxl::MakeCopyable([ snapshot, entries, callback = std::move(callback) ](
          ledger::Status status, auto new_entries, auto next_token) mutable {
        if (status != ledger::Status::OK &&
            status != ledger::Status::PARTIAL_RESULT) {
          callback(status);
          return;
        }
        for (auto& entry : new_entries) {
          entries->push_back(std::move(entry));
        }
        if (status == ledger::Status::OK) {
          callback(ledger::Status::OK);
          return;
        }
        GetEntriesRecursive(snapshot, entries, std::move(next_token),
                            std::move(callback));
      }));
}

}  // namespace

void GetEntries(ledger::PageSnapshot* const snapshot,
                std::vector<ledger::EntryPtr>* const entries,
                std::function<void(ledger::Status)> callback) {
  GetEntriesRecursive(snapshot, entries, nullptr /* next_token */,
                      std::move(callback));
}

}  // namespace modular
