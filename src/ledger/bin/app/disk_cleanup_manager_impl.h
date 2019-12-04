// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_DISK_CLEANUP_MANAGER_IMPL_H_
#define SRC_LEDGER_BIN_APP_DISK_CLEANUP_MANAGER_IMPL_H_

#include "src/ledger/bin/app/disk_cleanup_manager.h"
#include "src/ledger/bin/app/page_eviction_manager_impl.h"
#include "src/ledger/bin/app/page_usage_listener.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/platform/detached_path.h"
#include "src/ledger/bin/storage/public/db_factory.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {

class DiskCleanupManagerImpl : public DiskCleanupManager, public PageUsageListener {
 public:
  DiskCleanupManagerImpl(Environment* environment, PageUsageDb* db);
  DiskCleanupManagerImpl(const DiskCleanupManagerImpl&) = delete;
  DiskCleanupManagerImpl& operator=(const DiskCleanupManagerImpl&) = delete;
  ~DiskCleanupManagerImpl() override;

  // Sets the delegate for PageEvictionManager owned by DiskCleanupManagerImpl.
  // The delegate should outlive this object.
  void SetPageEvictionDelegate(PageEvictionManager::Delegate* delegate);

  // DiskCleanupManager:
  void SetOnDiscardable(fit::closure on_discardable) override;
  bool IsDiscardable() const override;
  void TryCleanUp(fit::function<void(Status)> callback) override;

  // PageUsageListener:
  void OnExternallyUsed(absl::string_view ledger_name, storage::PageIdView page_id) override;
  void OnExternallyUnused(absl::string_view ledger_name, storage::PageIdView page_id) override;
  void OnInternallyUsed(absl::string_view ledger_name, storage::PageIdView page_id) override;
  void OnInternallyUnused(absl::string_view ledger_name, storage::PageIdView page_id) override;

 private:
  // The state of a page while it is being used by at least one internal or external connection.
  struct PageState {
    int32_t internal_connections_count = 0;
    int32_t external_connections_count = 0;

    // Initially false. Becomes true if an external connection has been opened for this page. Never
    // changes back to false.
    bool is_eviction_candidate = false;
  };

  // If there are no active internal or external connections to the page, tries to evict it if is
  // potentially empty, and updates the pages state map by removing that entry.
  void HandlePageIfUnused(std::map<std::pair<std::string, storage::PageId>, PageState>::iterator it,
                          absl::string_view ledger_name, storage::PageIdView page_id);

  // Holds information about the state of pages that are currently open by internal or external
  // connections. Entries are removed if there are no active connections.
  std::map<std::pair<std::string, storage::PageId>, PageState> pages_state_;

  PageEvictionManagerImpl page_eviction_manager_;
  std::unique_ptr<PageEvictionPolicy> policy_;

  // TODO(nellyv): Add OnLowResources and OnPeriodicCleanUp to handle cleanup
  // opeations on the corresponding cases.
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_DISK_CLEANUP_MANAGER_IMPL_H_
