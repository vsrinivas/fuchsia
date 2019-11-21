// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_BACKGROUND_SYNC_MANAGER_H_
#define SRC_LEDGER_BIN_APP_BACKGROUND_SYNC_MANAGER_H_

#include "src/ledger/bin/app/page_usage_db.h"
#include "src/ledger/bin/app/page_usage_listener.h"
#include "src/ledger/bin/app/token_manager.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/lib/coroutine/coroutine_manager.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {

// Manages the synchronization of the closed pages with the cloud.
class BackgroundSyncManager : public PageUsageListener {
 public:
  // A Delegate, providing the necessary functionality to allow BackgroundSyncManager to trigger
  // sync with the cloud of the given page.
  class Delegate {
   public:
    virtual ~Delegate() = default;
    virtual void TrySyncClosedPage(absl::string_view ledger_name, storage::PageIdView page_id) = 0;
  };

  BackgroundSyncManager(Environment* environment, PageUsageDb* db);
  BackgroundSyncManager(Environment* environment, PageUsageDb* db, size_t open_pages_limit);
  BackgroundSyncManager(const BackgroundSyncManager&) = delete;
  BackgroundSyncManager& operator=(const BackgroundSyncManager&) = delete;

  // Sets the delegate for this BackgroundSyncManager. Must be called exactly once before any
  // PageUsageListener method is triggered.  The delegate should outlive this object.
  void SetDelegate(Delegate* delegate);

  // PageUsageListener:
  void OnExternallyUsed(absl::string_view ledger_name, storage::PageIdView page_id) override;
  void OnExternallyUnused(absl::string_view ledger_name, storage::PageIdView page_id) override;
  void OnInternallyUsed(absl::string_view ledger_name, storage::PageIdView page_id) override;
  void OnInternallyUnused(absl::string_view ledger_name, storage::PageIdView page_id) override;

  // Returns true, if there are no pending operations.
  bool IsDiscardable() const;

  void SetOnDiscardable(fit::closure on_discardable);

 private:
  // If there are no active internal or external connections to the page, tries to start
  // synchronization of closed pages, and and removes the entry from the pages state map. |it| must
  // be a valid iterator into |pages_connection_count_| map.
  void HandlePageIfUnused(std::map<std::pair<std::string, storage::PageId>, int32_t>::iterator it);

  // Triggers the start of the synchronization of closed pages.
  void TrySync();

  Environment* environment_;
  Delegate* sync_delegate_ = nullptr;
  PageUsageDb* db_;

  coroutine::CoroutineManager coroutine_manager_;

  // Holds information about the state of pages that are currently open by internal or external
  // connections. Entries are removed if there are no active connections.
  std::map<std::pair<std::string, storage::PageId>, int32_t> pages_connection_count_;
  // The number of pages that can be open at once. BackgroundSyncManager should not trigger
  // synchronization if current number of open pages is not less than the given limit.
  const size_t open_pages_limit_;

  // Preserves the object as long as it has a live token to avoid this class getting destructed in
  // the middle of call to Db.
  TokenManager token_manager_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_BACKGROUND_SYNC_MANAGER_H_
