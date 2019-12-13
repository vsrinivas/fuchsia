// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_PAGE_USAGE_DB_H_
#define SRC_LEDGER_BIN_APP_PAGE_USAGE_DB_H_

#include <functional>
#include <memory>

#include "src/ledger/bin/app/page_utils.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/clocks/impl/device_id_manager_impl.h"
#include "src/ledger/bin/clocks/public/types.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/storage/impl/leveldb.h"
#include "src/ledger/bin/storage/public/db.h"
#include "src/ledger/bin/storage/public/db_factory.h"
#include "src/ledger/bin/storage/public/iterator.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/synchronization/completer.h"
#include "src/ledger/lib/callback/operation_serializer.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/ledger/lib/timekeeper/clock.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

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
  PageUsageDb(Environment* environment, std::unique_ptr<storage::Db> db);
  PageUsageDb(const PageUsageDb&) = delete;
  PageUsageDb& operator=(const PageUsageDb&) = delete;
  ~PageUsageDb();

  // Marks the page with the given id as opened. |INTERNAL_ERROR| is returned if
  // the operation is interrupted.
  Status MarkPageOpened(coroutine::CoroutineHandler* handler, absl::string_view ledger_name,
                        storage::PageIdView page_id);

  // Marks the page with the given id as closed. |INTERNAL_ERROR| is returned if
  // the operation is interrupted.
  Status MarkPageClosed(coroutine::CoroutineHandler* handler, absl::string_view ledger_name,
                        storage::PageIdView page_id);

  // Marks the page with the given id as evicted. |INTERNAL_ERROR| is returned
  // if the operation is interrupted.
  Status MarkPageEvicted(coroutine::CoroutineHandler* handler, absl::string_view ledger_name,
                         storage::PageIdView page_id);

  // Marks all open pages as closed. |INTERNAL_ERROR| is returned if the
  // operation is interrupted.
  Status MarkAllPagesClosed(coroutine::CoroutineHandler* handler);

  // Updates |pages| to contain an iterator over all entries of page
  // information.
  Status GetPages(coroutine::CoroutineHandler* handler,
                  std::unique_ptr<storage::Iterator<const PageInfo>>* pages);

  // Returns true, if the initialization of this PageUsageDb was completed.
  bool IsInitialized();

 private:
  // Asynchronously initializes this PageUsageDb if needed.
  Status Init(coroutine::CoroutineHandler* handler);

  // Inserts the given |key|-|value| pair in the underlying database.
  Status Put(coroutine::CoroutineHandler* handler, absl::string_view key, absl::string_view value);

  // Deletes the row with the given |key| in the underlying database.
  Status Delete(coroutine::CoroutineHandler* handler, absl::string_view key);

  Clock* const clock_;
  std::unique_ptr<storage::Db> const db_;

  bool initialization_called_ = false;
  // The initialization completer. |Init| method starts marking pages as closed,
  // and returns before that operation is done. This completer makes sure that
  // all methods accessing the page usage database wait until the initialization
  // has finished, before reading or updating information.
  Completer initialization_completer_;

  // A serializer used for Put and Delete. Both these operations need to be
  // serialized to guarantee that consecutive calls to update the contents of a
  // single page (e.g. a page is opened and then closed) are written in |db_| in
  // the right order, i.e. the order in which they were called.
  OperationSerializer serializer_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_PAGE_USAGE_DB_H_
