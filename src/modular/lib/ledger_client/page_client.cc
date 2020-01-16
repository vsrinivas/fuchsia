// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/ledger_client/page_client.h"

#include <zircon/status.h>

#include <memory>
#include <utility>

#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/lib/fidl/array_to_string.h"
#include "src/modular/lib/ledger_client/ledger_client.h"

namespace modular {

PageClient::PageClient(std::string context, LedgerClient* ledger_client, LedgerPageId page_id,
                       std::string prefix)
    : binding_(this),
      context_(std::move(context)),
      ledger_client_(ledger_client),
      page_id_(std::move(page_id)),
      page_(ledger_client_->GetPage(this, context_, page_id_)),
      prefix_(std::move(prefix)) {
  fuchsia::ledger::PageSnapshotPtr snapshot;
  page_->GetSnapshot(snapshot.NewRequest(), to_array(prefix_), binding_.NewBinding());
}

PageClient::~PageClient() {
  // We assume ledger client always outlives page client.
  ledger_client_->DropPageClient(this);
}

fuchsia::ledger::PageSnapshotPtr PageClient::NewSnapshot() {
  fuchsia::ledger::PageSnapshotPtr ptr;
  ptr.set_error_handler([](zx_status_t status) {
    if (status != ZX_OK && status != ZX_ERR_PEER_CLOSED) {
      FXL_LOG(ERROR) << "PageSnapshot error: " << zx_status_get_string(status);
    }
  });
  page_->GetSnapshot(ptr.NewRequest(), to_array(prefix_), nullptr /* page_watcher */);
  return ptr;
}

// |PageWatcher|
void PageClient::OnChange(fuchsia::ledger::PageChange page,
                          fuchsia::ledger::ResultState result_state, OnChangeCallback callback) {
  // NOTE: |result_state| can indicate that this change notification is
  // partial: if a single FIDL message cannot contain the entire change
  // notification, the Ledger will break the notification into multiple chunks.
  // This is OK here because we break the notification down even further into
  // per-key calls to OnPageChange() and OnPageDelete().
  for (auto& entry : page.changed_entries) {
    // Remove key prefix maybe?
    OnPageChange(to_string(entry.key), std::move(entry.value));
  }

  for (auto& key : page.deleted_keys) {
    OnPageDelete(to_string(key));
  }

  callback(nullptr);
}

void PageClient::OnPageChange(const std::string& key, fuchsia::mem::BufferPtr value) {
  std::string value_string;
  if (fsl::StringFromVmo(*value, &value_string)) {
    OnPageChange(key, value_string);
  } else {
    FXL_LOG(ERROR) << "PageClient::OnChange() " << context_ << ": "
                   << "Unable to read/copy data.";
  }
}

void PageClient::OnPageChange(const std::string& /*key*/, const std::string& /*value*/) {}

void PageClient::OnPageDelete(const std::string& /*key*/) {}

void PageClient::OnPageConflict(Conflict* const conflict) {
  FXL_LOG(INFO) << "PageClient::OnPageConflict() " << context_ << " "
                << to_hex_string(conflict->key) << " " << conflict->left << " " << conflict->right;
};

namespace {

void GetEntriesRecursive(fuchsia::ledger::PageSnapshot* const snapshot,
                         std::vector<fuchsia::ledger::Entry>* const entries,
                         std::unique_ptr<fuchsia::ledger::Token> next_token,
                         fit::function<void()> done) {
  snapshot->GetEntries(
      std::vector<uint8_t>{} /* key_start */, std::move(next_token),
      [snapshot, entries, done = std::move(done)](auto new_entries, auto next_token) mutable {
        for (size_t i = 0; i < new_entries.size(); ++i) {
          entries->push_back(std::move(new_entries.at(i)));
        }

        if (!next_token) {
          done();
          return;
        }

        GetEntriesRecursive(snapshot, entries, std::move(next_token), std::move(done));
      });
}

}  // namespace

void GetEntries(fuchsia::ledger::PageSnapshot* const snapshot,
                std::vector<fuchsia::ledger::Entry>* const entries, fit::function<void()> done) {
  GetEntriesRecursive(snapshot, entries, nullptr /* next_token */, std::move(done));
}

}  // namespace modular
