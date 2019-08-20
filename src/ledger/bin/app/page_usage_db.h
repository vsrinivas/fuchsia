// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_PAGE_USAGE_DB_H_
#define SRC_LEDGER_BIN_APP_PAGE_USAGE_DB_H_

#include <lib/callback/operation_serializer.h>
#include <lib/timekeeper/clock.h>

#include <functional>
#include <memory>

#include "src/ledger/bin/app/page_utils.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/storage/impl/leveldb.h"
#include "src/ledger/bin/storage/public/db.h"
#include "src/ledger/bin/storage/public/db_factory.h"
#include "src/ledger/bin/storage/public/iterator.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/lib/fxl/strings/concatenate.h"

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
  PageUsageDb(timekeeper::Clock* clock, storage::DbFactory* db_factory, DetachedPath db_path);
  ~PageUsageDb();

  // Asynchronously initializes this PageUsageDb.
  Status Init(coroutine::CoroutineHandler* handler);

  // Marks the page with the given id as opened. |INTERNAL_ERROR| is returned if
  // the operation is interrupted.
  Status MarkPageOpened(coroutine::CoroutineHandler* handler, fxl::StringView ledger_name,
                        storage::PageIdView page_id);

  // Marks the page with the given id as closed. |INTERNAL_ERROR| is returned if
  // the operation is interrupted.
  Status MarkPageClosed(coroutine::CoroutineHandler* handler, fxl::StringView ledger_name,
                        storage::PageIdView page_id);

  // Marks the page with the given id as evicted. |INTERNAL_ERROR| is returned
  // if the operation is interrupted.
  Status MarkPageEvicted(coroutine::CoroutineHandler* handler, fxl::StringView ledger_name,
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
  // TODO(opalle): Consider using DelayingFacade instead of Completer.
  // A Completer allowing waiting until the target operation is completed.
  class Completer {
   public:
    Completer();

    ~Completer();

    // Completes the operation with the given status and unblocks all pending
    // |WaitUntilDone| calls. |Complete| can only be called once.
    void Complete(Status status);

    // Cancels the operation. All WaitUntilDone calls will return
    // |Status::INTERRUPTED|. If |Cancel| is called, |Complete| should
    // never be called.
    void Cancel();

    // Blocks execution until |Complete| is called, and then returns its status.
    // If the operation is already completed, |WaitUntilDone| returns
    // immediately with the result status.
    Status WaitUntilDone(coroutine::CoroutineHandler* handler);

    // Returns true, if the operation was completed.
    bool IsCompleted();

   private:
    // Marks the Completer as completed with the given status and calls the
    // pending callbacks.
    void CallCallbacks(Status status);

    bool completed_ = false;
    Status status_;
    // Closures invoked upon completion to unblock the waiting coroutines.
    std::vector<fit::closure> callbacks_;

    FXL_DISALLOW_COPY_AND_ASSIGN(Completer);
  };

  // Inserts the given |key|-|value| pair in the underlying database.
  Status Put(coroutine::CoroutineHandler* handler, fxl::StringView key, fxl::StringView value);

  // Deletes the row with the given |key| in the underlying database.
  Status Delete(coroutine::CoroutineHandler* handler, fxl::StringView key);

  timekeeper::Clock* clock_;
  // |db_factory_| and |db_path_| should only be used during initialization.
  // After Init() has been called their contents are no longer valid.
  storage::DbFactory* db_factory_;
  DetachedPath db_path_;
  std::unique_ptr<storage::Db> db_;
  // The initialization completer. |Init| method starts marking pages as closed,
  // and returns before that operation is done. This completer makes sure that
  // all methods accessing the page usage database wait until the initialization
  // has finished, before reading or updating information.
  PageUsageDb::Completer initialization_completer_;

  // A serializer used for Put and Delete. Both these operations need to be
  // serialized to guarantee that consecutive calls to update the contents of a
  // single page (e.g. a page is opened and then closed) are written in |db_| in
  // the right order, i.e. the order in which they were called.
  callback::OperationSerializer serializer_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageUsageDb);
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_PAGE_USAGE_DB_H_
