// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/fidl/page_client.h"

#include "apps/modular/lib/fidl/array_to_string.h"
#include "lib/mtl/vmo/strings.h"

namespace modular {

PageClient::PageClient(const std::string& context,
                       ledger::Page* const page,
                       const char* const prefix) :
    binding_(this),
    context_(context) {
  FTL_DCHECK(page);
  page->GetSnapshot(
      NewRequest(),
      prefix == nullptr ? nullptr : to_array(prefix), binding_.NewBinding(),
      [this](ledger::Status status) {
        if (status != ledger::Status::OK) {
          FTL_LOG(ERROR) << context_ << " Page.GetSnapshot() " << status;
        }
      });
}

PageClient::~PageClient() = default;

fidl::InterfaceRequest<ledger::PageSnapshot> PageClient::NewRequest() {
  page_snapshot_.reset(new ledger::PageSnapshotPtr);
  auto ret = (*page_snapshot_).NewRequest();
  (*page_snapshot_).set_connection_error_handler([this] {
    FTL_LOG(ERROR) << context_ << ": "
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
  if (!page.is_null() && !page->changes.is_null()) {
    for (auto& entry : page->changes) {
      // Remove prefix maybe?
      const std::string& key = to_string(entry->key);
      std::string value;
      if (!mtl::StringFromVmo(entry->value, &value)) {
        FTL_LOG(ERROR) << "PageClient::OnChange() "
                       << context_ << ": "
                       << "Unable to extract data.";
        continue;
      }

      OnChange(key, value);
    }

    for (auto& key : page->deleted_keys) {
      OnDelete(to_string(key));
    }
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

void PageClient::OnChange(const std::string& key, const std::string& value) {}

void PageClient::OnDelete(const std::string& key) {}

}  // namespace modular
