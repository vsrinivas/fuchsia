// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_BACKGROUND_SYNC_MANAGER_H_
#define SRC_LEDGER_BIN_APP_BACKGROUND_SYNC_MANAGER_H_

#include "src/ledger/bin/app/page_usage_db.h"
#include "src/ledger/bin/app/page_usage_listener.h"
#include "src/ledger/bin/app/token_manager.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/lib/coroutine/coroutine_manager.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace ledger {

// Manages the synchronization of the closed pages with the cloud.
class BackgroundSyncManager : public PageUsageListener {
 public:
  // A Delegate, providing the necessary functionality to allow BackgroundSyncManager to trigger
  // sync with the cloud of the given page.
  class Delegate {
   public:
    virtual void TrySyncClosedPage(fxl::StringView ledger_name, storage::PageIdView page_id) = 0;
  };

  BackgroundSyncManager(Environment* environment, PageUsageDb* db);
  BackgroundSyncManager(Environment* environment, PageUsageDb* db, size_t open_pages_limit);
  BackgroundSyncManager(const BackgroundSyncManager&) = delete;
  BackgroundSyncManager& operator=(const BackgroundSyncManager&) = delete;

  // Sets the delegate for this BackgroundSyncManager. Must be called exactly once before any
  // PageUsageListener method is triggered.  The delegate should outlive this object.
  void SetDelegate(Delegate* delegate);

  // PageUsageListener:
  void OnExternallyUsed(fxl::StringView ledger_name, storage::PageIdView page_id) override;
  void OnExternallyUnused(fxl::StringView ledger_name, storage::PageIdView page_id) override;
  void OnInternallyUsed(fxl::StringView ledger_name, storage::PageIdView page_id) override;
  void OnInternallyUnused(fxl::StringView ledger_name, storage::PageIdView page_id) override;

  // Returns true, if there are no pending operations.
  bool IsEmpty();

  void set_on_empty(fit::closure on_empty_callback) {
    on_empty_callback_ = std::move(on_empty_callback);
  }

 private:
  // The state of a page while it is being used by at least one internal or external connection.
  struct PageState {
    bool has_internal_connections = false;
    bool has_external_connections = false;
  };

  // If there are no active internal or external connections to the page, tries to start
  // synchronization of closed pages, and and removes the entry from the pages state map. |it| must
  // be a valid iterator into |pages_state_map_|.
  void HandlePageIfUnused(
      std::map<std::pair<std::string, storage::PageId>, PageState>::iterator it);

  // Triggers the start of the synchronization of closed pages.
  void TrySync();

  // TODO(https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=35654): Replace this with
  // TokenManager, once it supports dispatching a task.
  //
  // Preserves the object as long as it has a live token to avoid this class getting destructed in
  // the middle of call to Db.
  ExpiringToken NewExpiringToken();
  // A counter of currently outgoing calls to Db.
  ssize_t pending_operations_ = 0;

  Environment* environment_;
  Delegate* sync_delegate_ = nullptr;
  PageUsageDb* db_;

  coroutine::CoroutineManager coroutine_manager_;

  fit::closure on_empty_callback_;

  // Holds information about the state of pages that are currently open by internal or external
  // connections. Entries are removed if there are no active connections.
  std::map<std::pair<std::string, storage::PageId>, PageState> pages_state_;
  // The number of pages that can be open at once. BackgroundSyncManager should not trigger
  // synchronization if current number of open pages is not less than the given limit.
  const size_t open_pages_limit_;

  // Must be the last member.
  fxl::WeakPtrFactory<BackgroundSyncManager> weak_factory_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_BACKGROUND_SYNC_MANAGER_H_
