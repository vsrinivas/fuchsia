// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_PAGE_USAGE_DB_H_
#define PERIDOT_BIN_LEDGER_APP_PAGE_USAGE_DB_H_

#include <zx/time.h>
#include <functional>
#include <memory>

#include "lib/fxl/strings/concatenate.h"
#include "peridot/bin/ledger/app/page_utils.h"
#include "peridot/bin/ledger/coroutine/coroutine.h"
#include "peridot/bin/ledger/storage/impl/db.h"
#include "peridot/bin/ledger/storage/impl/leveldb.h"
#include "peridot/bin/ledger/storage/public/iterator.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace ledger {

// |PageUsageDb| persists all information on page usage.
//
// Calls to |MarkPageOpened| and |MarkPageClosed| will update the underlying
// database in the order in which they are called.
//
// Rows in the underlying database are serialized as follows:
// Last usage row:
// - Key: "opened/<ledger_name><page_id>"
// - Value: "<timestamp>" or timestamp 0 for open pages
class PageUsageDb {
 public:
  // Holds information on when a page was last used.
  struct PageInfo {
    std::string ledger_name;
    storage::PageId page_id;
    // The timestamp in UTC of when the page was last closed, as an indication
    // of when it was last used. If the page is currently open, the value is set
    // to zx::time(0).
    zx::time timestamp;
  };

  PageUsageDb(async_dispatcher_t* dispatcher, ledger::DetachedPath db_path);
  ~PageUsageDb();

  // Initializes the underlying database. Init should be called before any other
  // operation is performed.
  Status Init();

  // Marks the page with the given id as opened.
  Status MarkPageOpened(coroutine::CoroutineHandler* handler,
                        fxl::StringView ledger_name,
                        storage::PageIdView page_id);

  // Marks the page with the given id as closed.
  Status MarkPageClosed(coroutine::CoroutineHandler* handler,
                        fxl::StringView ledger_name,
                        storage::PageIdView page_id);

  // Marks the page with the given id as evicted.
  Status MarkPageEvicted(coroutine::CoroutineHandler* handler,
                         fxl::StringView ledger_name,
                         storage::PageIdView page_id);

  // Marks all open pages as closed.
  Status MarkAllPagesClosed(coroutine::CoroutineHandler* handler);

  // Updates |pages| to contain an iterator over all entries of page
  // information.
  Status GetPages(
      coroutine::CoroutineHandler* handler,
      std::unique_ptr<storage::Iterator<const PageUsageDb::PageInfo>>* pages);

 private:
  // Inserts the given |key|-|value| pair in the underlying database.
  Status Put(coroutine::CoroutineHandler* handler, fxl::StringView key,
             fxl::StringView value);

  // Deletes the row with the given |key| in the underlying database.
  Status Delete(coroutine::CoroutineHandler* handler, fxl::StringView key);

  storage::LevelDb db_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageUsageDb);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_PAGE_USAGE_DB_H_
