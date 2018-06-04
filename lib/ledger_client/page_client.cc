// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/ledger_client/page_client.h"

#include <memory>
#include <utility>

#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/functional/make_copyable.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/ledger_client/ledger_client.h"

namespace fuchsia {
namespace modular {

PageClient::PageClient(std::string context, LedgerClient* ledger_client,
                       LedgerPageId page_id, std::string prefix)
    : binding_(this),
      context_(std::move(context)),
      ledger_client_(ledger_client),
      page_id_(std::move(page_id)),
      page_(ledger_client_->GetPage(this, context_, page_id_)),
      prefix_(std::move(prefix)) {
  page_->GetSnapshot(NewRequest(), to_array(prefix_), binding_.NewBinding(),
                     [this](fuchsia::ledger::Status status) {
                       if (status != fuchsia::ledger::Status::OK) {
                         FXL_LOG(ERROR)
                             << context_ << " Page.GetSnapshot() " << status;
                       }
                     });
}

PageClient::~PageClient() {
  // We assume ledger client always outlives page client.
  ledger_client_->DropPageClient(this);
}

fidl::InterfaceRequest<fuchsia::ledger::PageSnapshot> PageClient::NewRequest() {
  page_snapshot_ = std::make_shared<fuchsia::ledger::PageSnapshotPtr>();
  auto ret = (*page_snapshot_).NewRequest();
  (*page_snapshot_).set_error_handler([this] {
    FXL_LOG(ERROR) << context_ << ": "
                   << "PageSnapshot connection unexpectedly closed.";
  });
  return ret;
}

fidl::InterfaceRequest<fuchsia::ledger::PageSnapshot>
PageClient::MaybeUpdateSnapshot(
    const fuchsia::ledger::ResultState result_state) {
  switch (result_state) {
    case fuchsia::ledger::ResultState::PARTIAL_CONTINUED:
    case fuchsia::ledger::ResultState::PARTIAL_STARTED:
      return nullptr;

    case fuchsia::ledger::ResultState::COMPLETED:
    case fuchsia::ledger::ResultState::PARTIAL_COMPLETED:
      return NewRequest();
  }
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

  // Every time we receive a group of OnChange notifications, we update the root
  // page snapshot so we see the current state. Note that pending Operation
  // instances may hold on to the previous value until they finish. New
  // Operation instances created after the update receive the new snapshot.
  //
  // For continued updates, we only request the snapshot once, in the last
  // OnChange() notification.
  callback(MaybeUpdateSnapshot(result_state));
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
}  // namespace fuchsia
