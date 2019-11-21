// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_PAGE_EVICTION_MANAGER_IMPL_H_
#define SRC_LEDGER_BIN_APP_PAGE_EVICTION_MANAGER_IMPL_H_

#include <lib/fit/function.h>

#include <memory>
#include <utility>

#include "src/ledger/bin/app/page_eviction_manager.h"
#include "src/ledger/bin/app/page_usage_db.h"
#include "src/ledger/bin/app/page_utils.h"
#include "src/ledger/bin/app/token_manager.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/storage/public/db_factory.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/ledger/lib/coroutine/coroutine_manager.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {

class PageEvictionManagerImpl : public PageEvictionManager, public PageEvictionDelegate {
 public:
  PageEvictionManagerImpl(Environment* environment, PageUsageDb* db);
  PageEvictionManagerImpl(const PageEvictionManagerImpl&) = delete;
  PageEvictionManagerImpl& operator=(const PageEvictionManagerImpl&) = delete;
  ~PageEvictionManagerImpl() override;

  // Sets the delegate for this PageEvictionManagerImpl. The delegate should
  // outlive this object.
  void SetDelegate(PageEvictionManager::Delegate* delegate);

  // PageEvictionManager:
  void SetOnDiscardable(fit::closure on_discardable) override;

  bool IsDiscardable() const override;

  void TryEvictPages(PageEvictionPolicy* policy, fit::function<void(Status)> callback) override;

  void MarkPageOpened(absl::string_view ledger_name, storage::PageIdView page_id) override;

  void MarkPageClosed(absl::string_view ledger_name, storage::PageIdView page_id) override;

  // PageEvictionDelegate:
  void TryEvictPage(absl::string_view ledger_name, storage::PageIdView page_id,
                    PageEvictionCondition condition,
                    fit::function<void(Status, PageWasEvicted)> callback) override;

 private:
  // Removes the page from the local storage. The caller of this method must
  // ensure that the given page exists.
  void EvictPage(absl::string_view ledger_name, storage::PageIdView page_id,
                 fit::function<void(Status)> callback);

  // Checks whether a page can be evicted. A page can be evicted if it is
  // currently closed and either:
  // - has no unsynced commits or objects, or
  // - is empty and offline, i.e. was never synced to the cloud or a peer.
  Status CanEvictPage(coroutine::CoroutineHandler* handler, absl::string_view ledger_name,
                      storage::PageIdView page_id, bool* can_evict);

  // Checks whether a page is closed, offline and empty, and thus can be
  // evicted.
  Status CanEvictEmptyPage(coroutine::CoroutineHandler* handler, absl::string_view ledger_name,
                           storage::PageIdView page_id, bool* can_evict);

  // Marks the given page as evicted in the page usage database.
  void MarkPageEvicted(std::string ledger_name, storage::PageId page_id);

  Status SynchronousTryEvictPage(coroutine::CoroutineHandler* handler, std::string ledger_name,
                                 storage::PageId page_id, PageEvictionCondition condition,
                                 PageWasEvicted* was_evicted);

  Environment* environment_;
  PageEvictionManager::Delegate* delegate_ = nullptr;
  PageUsageDb* db_;
  coroutine::CoroutineManager coroutine_manager_;
  TokenManager token_manager_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_PAGE_EVICTION_MANAGER_IMPL_H_
