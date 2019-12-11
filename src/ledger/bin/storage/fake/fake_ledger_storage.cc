// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/ledger/bin/storage/fake/fake_ledger_storage.h"

#include <lib/async/cpp/task.h>

#include "src/ledger/bin/storage/fake/fake_page_storage.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/logging/logging.h"

namespace storage {
namespace fake {

DelayingCallbacksManager::DelayingCallbacksManager() = default;
DelayingCallbacksManager::~DelayingCallbacksManager() = default;

class DelayIsSyncedCallbackFakePageStorage : public storage::fake::FakePageStorage {
 public:
  explicit DelayIsSyncedCallbackFakePageStorage(
      ledger::Environment* environment, DelayingCallbacksManager* delaying_callbacks_manager,
      storage::PageId id)
      : storage::fake::FakePageStorage(environment, id),
        delaying_callbacks_manager_(delaying_callbacks_manager) {}
  DelayIsSyncedCallbackFakePageStorage(const DelayIsSyncedCallbackFakePageStorage&) = delete;
  DelayIsSyncedCallbackFakePageStorage& operator=(const DelayIsSyncedCallbackFakePageStorage&) =
      delete;
  ~DelayIsSyncedCallbackFakePageStorage() override = default;

  // Unblocks the call of |IsSynced| callback for the given page.
  void CallIsSyncedCallback() {
    storage::fake::FakePageStorage::IsSynced(std::move(is_synced_callback_));
  }

  // FakePageStorage:
  void IsSynced(fit::function<void(ledger::Status, bool)> callback) override {
    if (!delaying_callbacks_manager_->ShouldDelayIsSyncedCallback(page_id_)) {
      storage::fake::FakePageStorage::IsSynced(std::move(callback));
      return;
    }
    is_synced_callback_ = std::move(callback);
  }

  void IsEmpty(fit::function<void(ledger::Status, bool)> callback) override {
    callback(ledger::Status::OK, true);
  }

  bool IsOnline() override { return false; }

 private:
  fit::function<void(ledger::Status, bool)> is_synced_callback_;
  DelayingCallbacksManager* delaying_callbacks_manager_;
};

FakeLedgerStorage::FakeLedgerStorage(ledger::Environment* environment)
    : environment_(environment) {}
FakeLedgerStorage::~FakeLedgerStorage() = default;

void FakeLedgerStorage::ListPages(
    fit::function<void(storage::Status, std::set<storage::PageId>)> callback) {
  LEDGER_NOTREACHED() << "Maybe implement this later on if needed?";
}

void FakeLedgerStorage::CreatePageStorage(
    storage::PageId page_id,
    fit::function<void(ledger::Status, std::unique_ptr<storage::PageStorage>)> callback) {
  create_page_calls.push_back(std::move(page_id));
  callback(ledger::Status::IO_ERROR, nullptr);
}

void FakeLedgerStorage::GetPageStorage(
    storage::PageId page_id,
    fit::function<void(ledger::Status, std::unique_ptr<storage::PageStorage>)> callback) {
  get_page_calls.push_back(page_id);
  async::PostTask(
      environment_->dispatcher(), [this, callback = std::move(callback), page_id]() mutable {
        if (should_get_page_fail) {
          callback(ledger::Status::PAGE_NOT_FOUND, nullptr);
        } else {
          auto fake_page_storage =
              std::make_unique<DelayIsSyncedCallbackFakePageStorage>(environment_, this, page_id);
          // If the page was opened before, restore the previous sync state.
          fake_page_storage->set_synced(synced_pages_.find(page_id) != synced_pages_.end());
          page_storages_[std::move(page_id)] = fake_page_storage.get();
          callback(ledger::Status::OK, std::move(fake_page_storage));
        }
      });
}

void FakeLedgerStorage::DeletePageStorage(storage::PageIdView /*page_id*/,
                                          fit::function<void(ledger::Status)> callback) {
  delete_page_storage_callback = std::move(callback);
}

void FakeLedgerStorage::ClearCalls() {
  create_page_calls.clear();
  get_page_calls.clear();
  page_storages_.clear();
}

void FakeLedgerStorage::DelayIsSyncedCallback(storage::PageIdView page_id, bool delay_callback) {
  if (delay_callback) {
    pages_with_delayed_callback.insert(convert::ToString(page_id));
  } else {
    pages_with_delayed_callback.erase(convert::ToString(page_id));
  }
}

bool FakeLedgerStorage::ShouldDelayIsSyncedCallback(storage::PageIdView page_id) {
  return pages_with_delayed_callback.find(convert::ToString(page_id)) !=
         pages_with_delayed_callback.end();
}

void FakeLedgerStorage::CallIsSyncedCallback(storage::PageIdView page_id) {
  auto it = page_storages_.find(convert::ToString(page_id));
  LEDGER_CHECK(it != page_storages_.end());
  it->second->CallIsSyncedCallback();
}

void FakeLedgerStorage::set_page_storage_synced(storage::PageIdView page_id, bool is_synced) {
  storage::PageId page_id_string = convert::ToString(page_id);
  if (is_synced) {
    synced_pages_.insert(page_id_string);
  } else {
    auto it = synced_pages_.find(page_id_string);
    if (it != synced_pages_.end()) {
      synced_pages_.erase(it);
    }
  }

  LEDGER_CHECK(page_storages_.find(page_id_string) != page_storages_.end());
  page_storages_[page_id_string]->set_synced(is_synced);
}

void FakeLedgerStorage::set_page_storage_offline_empty(storage::PageIdView page_id,
                                                       bool is_offline_empty) {
  storage::PageId page_id_string = convert::ToString(page_id);
  if (is_offline_empty) {
    offline_empty_pages_.insert(page_id_string);
  } else {
    auto it = offline_empty_pages_.find(page_id_string);
    if (it != offline_empty_pages_.end()) {
      offline_empty_pages_.erase(it);
    }
  }
}

}  // namespace fake
}  // namespace storage
