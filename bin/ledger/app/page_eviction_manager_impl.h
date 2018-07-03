// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_PAGE_EVICTION_MANAGER_IMPL_H_
#define PERIDOT_BIN_LEDGER_APP_PAGE_EVICTION_MANAGER_IMPL_H_

#include "peridot/bin/ledger/app/page_eviction_manager.h"

#include <utility>

#include <lib/fit/function.h>

#include "peridot/bin/ledger/app/page_state_reader.h"
#include "peridot/bin/ledger/coroutine/coroutine.h"

namespace ledger {

class PageEvictionManagerImpl : public PageEvictionManager {
 public:
  explicit PageEvictionManagerImpl(
      coroutine::CoroutineService* coroutine_service);
  ~PageEvictionManagerImpl() override;

  // Initializes this PageEvictionManager. |IO_ERROR| will be returned in case
  // of an error while initializing the underlying database.
  Status Init();

  void SetPageStateReader(PageStateReader* state_reader);

  // PageEvictionManager:
  void TryCleanUp(fit::function<void(Status)> callback) override;

  void OnPageOpened(fxl::StringView ledger_name,
                    storage::PageIdView page_id) override;

  void OnPageClosed(fxl::StringView ledger_name,
                    storage::PageIdView page_id) override;

 private:
  Status EvictPage(fxl::StringView ledger_name, storage::PageIdView page_id);

  // Checks whether a page can be evicted. We can evict pages that are not
  // currently used and have no unsynced commits or objects.
  Status CanEvictPage(coroutine::CoroutineHandler* handler,
                      fxl::StringView ledger_name, storage::PageIdView page_id,
                      bool* can_evict);

  PageStateReader* state_reader_ = nullptr;
  coroutine::CoroutineService* coroutine_service_;

  // For each page, stores the timestamp from when it was last used. The key is
  // a pair containing the ledger name and page id respectively, while the value
  // is the timestamp. If a page is currently in use, value will be zx::time(0).
  // TODO(nellyv): this information should be stored on disk instead.
  std::map<std::pair<std::string, std::string>, zx::time> last_used_map_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageEvictionManagerImpl);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_PAGE_EVICTION_MANAGER_IMPL_H_
