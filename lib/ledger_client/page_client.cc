// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/ledger_client/page_client.h"

#include <memory>
#include <utility>

#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/functional/make_copyable.h>

#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/ledger_client/ledger_client.h"

namespace modular {

PageClient::PageClient(std::string context, LedgerClient* ledger_client,
                       LedgerPageId page_id, std::string prefix)
    : binding_(this),
      context_(std::move(context)),
      ledger_client_(ledger_client),
      page_id_(std::move(page_id)),
      page_(ledger_client_->GetPage(this, context_, page_id_)),
      prefix_(std::move(prefix)) {
  fuchsia::ledger::PageSnapshotPtr snapshot;
  page_->GetSnapshot(
      snapshot.NewRequest(), to_array(prefix_), binding_.NewBinding(),
      [this](fuchsia::ledger::Status status) {
        if (status != fuchsia::ledger::Status::OK) {
          FXL_LOG(ERROR) << context_ << " Page.GetSnapshot() " << status;
        }
      });
}

PageClient::~PageClient() {
  // We assume ledger client always outlives page client.
  ledger_client_->DropPageClient(this);
}

fuchsia::ledger::PageSnapshotPtr PageClient::NewSnapshot(
    std::function<void()> on_error) {
  fuchsia::ledger::PageSnapshotPtr ptr;
  page_->GetSnapshot(
      ptr.NewRequest(), to_array(prefix_), nullptr /* page_watcher */,
      [this, on_error = std::move(on_error)](fuchsia::ledger::Status status) {
        if (status != fuchsia::ledger::Status::OK) {
          FXL_LOG(ERROR) << context_ << " Page.GetSnapshot() " << status;
          on_error();
        }
      });
  return ptr;
}

// |PageWatcher|
void PageClient::OnChange(fuchsia::ledger::PageChange page,
                          fuchsia::ledger::ResultState result_state,
                          OnChangeCallback callback) {
  // According to their fidl spec, neither page nor page->changed_entries
  // should be null.
  FXL_DCHECK(page.changed_entries);
  for (auto& entry : *page.changed_entries) {
    // Remove prefix maybe?
    const std::string key = to_string(entry.key);
    std::string value;
    if (!fsl::StringFromVmo(*entry.value, &value)) {
      FXL_LOG(ERROR) << "PageClient::OnChange() " << context_ << ": "
                     << "Unable to extract data.";
      continue;
    }

    OnPageChange(key, value);
  }

  for (auto& key : *page.deleted_keys) {
    OnPageDelete(to_string(key));
  }

  callback(nullptr);
}

void PageClient::OnPageChange(const std::string& /*key*/,
                              const std::string& /*value*/) {}

void PageClient::OnPageDelete(const std::string& /*key*/) {}

void PageClient::OnPageConflict(Conflict* const conflict) {
  FXL_LOG(INFO) << "PageClient::OnPageConflict() " << context_ << " "
                << to_hex_string(conflict->key) << " " << conflict->left << " "
                << conflict->right;
};

namespace {

void GetEntriesRecursive(fuchsia::ledger::PageSnapshot* const snapshot,
                         std::vector<fuchsia::ledger::Entry>* const entries,
                         std::unique_ptr<fuchsia::ledger::Token> next_token,
                         std::function<void(fuchsia::ledger::Status)> done) {
  snapshot->GetEntries(
      fidl::VectorPtr<uint8_t>::New(0) /* key_start */, std::move(next_token),
      fxl::MakeCopyable([snapshot, entries, done = std::move(done)](
                            fuchsia::ledger::Status status, auto new_entries,
                            auto next_token) mutable {
        if (status != fuchsia::ledger::Status::OK &&
            status != fuchsia::ledger::Status::PARTIAL_RESULT) {
          done(status);
          return;
        }

        for (size_t i = 0; i < new_entries->size(); ++i) {
          entries->push_back(std::move(new_entries->at(i)));
        }

        if (status == fuchsia::ledger::Status::OK) {
          done(fuchsia::ledger::Status::OK);
          return;
        }

        GetEntriesRecursive(snapshot, entries, std::move(next_token),
                            std::move(done));
      }));
}

}  // namespace

void GetEntries(fuchsia::ledger::PageSnapshot* const snapshot,
                std::vector<fuchsia::ledger::Entry>* const entries,
                std::function<void(fuchsia::ledger::Status)> done) {
  GetEntriesRecursive(snapshot, entries, nullptr /* next_token */,
                      std::move(done));
}

}  // namespace modular
