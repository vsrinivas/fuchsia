// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_LEDGER_STORAGE_H_
#define SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_LEDGER_STORAGE_H_

#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/storage/public/ledger_storage.h"
#include "src/ledger/bin/storage/public/types.h"

namespace storage {
namespace fake {

// Manages delays of page sync callbacks.
class DelayingCallbacksManager {
 public:
  DelayingCallbacksManager();
  DelayingCallbacksManager(const DelayingCallbacksManager&) = delete;
  DelayingCallbacksManager& operator=(const DelayingCallbacksManager&) = delete;
  virtual ~DelayingCallbacksManager();

  // Returns true if the PageStorage of the page with the given id should delay
  // calling the callback of |IsSynced|.
  virtual bool ShouldDelayIsSyncedCallback(storage::PageIdView page_id) = 0;
};

// Provides functionality for blocking |IsSynced| callback for a page.
class DelayIsSyncedCallbackFakePageStorage;

class FakeLedgerStorage : public storage::LedgerStorage,
                          public storage::fake::DelayingCallbacksManager {
 public:
  explicit FakeLedgerStorage(ledger::Environment* environment);
  FakeLedgerStorage(const FakeLedgerStorage&) = delete;
  FakeLedgerStorage& operator=(const FakeLedgerStorage&) = delete;
  ~FakeLedgerStorage() override;

  // Removes the stored information about calls of LedgerStorage methods and clears the container of
  // PageStorage objects.
  void ClearCalls();

  // Keeps track of pages with delayed callback.
  void DelayIsSyncedCallback(storage::PageIdView page_id, bool delay_callback);

  // Triggers the call of |IsSynced| callback for the given page.
  void CallIsSyncedCallback(storage::PageIdView page_id);

  // LedgerStorage:
  void ListPages(fit::function<void(storage::Status, std::set<storage::PageId>)> callback) override;

  void CreatePageStorage(
      storage::PageId page_id,
      fit::function<void(ledger::Status, std::unique_ptr<storage::PageStorage>)> callback) override;

  void GetPageStorage(
      storage::PageId page_id,
      fit::function<void(ledger::Status, std::unique_ptr<storage::PageStorage>)> callback) override;

  void DeletePageStorage(storage::PageIdView /*page_id*/,
                         fit::function<void(Status)> callback) override;

  // DelayingCallbacksManager:
  bool ShouldDelayIsSyncedCallback(storage::PageIdView page_id) override;

  void set_page_storage_synced(storage::PageIdView page_id, bool is_synced);

  void set_page_storage_offline_empty(storage::PageIdView page_id, bool is_offline_empty);

  bool should_get_page_fail = false;
  std::vector<storage::PageId> create_page_calls;
  std::vector<storage::PageId> get_page_calls;
  fit::function<void(ledger::Status)> delete_page_storage_callback;

 private:
  ledger::Environment* const environment_;
  std::map<storage::PageId, DelayIsSyncedCallbackFakePageStorage*> page_storages_;
  std::set<storage::PageId> synced_pages_;
  std::set<storage::PageId> offline_empty_pages_;
  std::set<storage::PageId> pages_with_delayed_callback;
};

}  // namespace fake
}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_LEDGER_STORAGE_H_
