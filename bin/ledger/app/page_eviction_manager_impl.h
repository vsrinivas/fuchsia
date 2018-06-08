// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_PAGE_EVICTION_MANAGER_IMPL_H_
#define PERIDOT_BIN_LEDGER_APP_PAGE_EVICTION_MANAGER_IMPL_H_

#include <utility>

#include "peridot/bin/ledger/app/page_eviction_manager.h"

namespace ledger {

class PageEvictionManagerImpl : public PageEvictionManager {
 public:
  PageEvictionManagerImpl();
  ~PageEvictionManagerImpl();

  // Initializes this PageEvictionManager. |IO_ERROR| will be returned in case
  // of an error while initializing the underlying database.
  Status Init();

  // PageEvictionManager:
  void TryCleanUp(std::function<void(Status)> callback) override;

  void OnPageOpened(fxl::StringView ledger_name,
                    storage::PageIdView page_id) override;

  void OnPageClosed(fxl::StringView ledger_name,
                    storage::PageIdView page_id) override;

 private:
  Status EvictPage(fxl::StringView ledger_name, storage::PageIdView page_id);

  Status PageIsSynced(fxl::StringView ledger_name, storage::PageIdView page_id,
                      bool* is_synced);

  // For each page, stores the timestamp from when it was last used. The key is
  // a pair containing the ledger name and page id respectively, while the value
  // is the timestamp. If a page is currently in use, value will be zx::time(0).
  // TODO(nellyv): this information should be stored on disk instead.
  std::map<std::pair<std::string, std::string>, zx::time> last_used_map_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageEvictionManagerImpl);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_PAGE_EVICTION_MANAGER_IMPL_H_
