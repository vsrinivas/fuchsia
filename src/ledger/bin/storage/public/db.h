// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_PUBLIC_DB_H_
#define SRC_LEDGER_BIN_STORAGE_PUBLIC_DB_H_

#include "src/ledger/bin/storage/public/iterator.h"
#include "src/ledger/bin/storage/public/object.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/coroutine/coroutine.h"

namespace storage {

class Db {
 public:
  class Batch {
   public:
    // A |Batch| can be used to execute a number of updates in |Db| atomically.
    Batch() {}
    virtual ~Batch() {}

    // Inserts the given key-value pair in the database.
    FXL_WARN_UNUSED_RESULT virtual Status Put(
        coroutine::CoroutineHandler* handler, convert::ExtendedStringView key,
        fxl::StringView value) = 0;

    // Deletes the entry in the database with the given |key|.
    FXL_WARN_UNUSED_RESULT virtual Status Delete(
        coroutine::CoroutineHandler* handler,
        convert::ExtendedStringView key) = 0;

    // Executes this batch. No further operations in this batch are supported
    // after a successful execution.
    FXL_WARN_UNUSED_RESULT virtual Status Execute(
        coroutine::CoroutineHandler* handler) = 0;

   private:
    FXL_DISALLOW_COPY_AND_ASSIGN(Batch);
  };

  Db() {}
  virtual ~Db() {}

  // Starts a new batch. The batch will be written when Execute is called on the
  // returned object. The Db object must outlive the batch object.
  // The handler (and the corresponding coroutine) only need to remain active
  // until the result is returned. If the coroutine is interrupted,
  // |INTERRUPTED| status is returned.
  FXL_WARN_UNUSED_RESULT virtual Status StartBatch(
      coroutine::CoroutineHandler* handler, std::unique_ptr<Batch>* batch) = 0;

  // Retrieves the value corresponding to |key|.
  FXL_WARN_UNUSED_RESULT virtual Status Get(
      coroutine::CoroutineHandler* handler, convert::ExtendedStringView key,
      std::string* value) = 0;

  // Checks whether |key| is stored in this database. Returns |OK| if the key
  // was found, |INTERNAL_NOT_FOUND| if not, or another type of error in case of
  // failure to look up.
  FXL_WARN_UNUSED_RESULT virtual Status HasKey(
      coroutine::CoroutineHandler* handler,
      convert::ExtendedStringView key) = 0;

  // Retrieves the value for the given |key| as an Object with the provided
  // |object_identifier|.
  FXL_WARN_UNUSED_RESULT virtual Status GetObject(
      coroutine::CoroutineHandler* handler, convert::ExtendedStringView key,
      ObjectIdentifier object_identifier,
      std::unique_ptr<const Object>* object) = 0;

  // Retrieves all keys matching the given |prefix|. |key_suffixes| will be
  // updated to contain the suffixes of corresponding keys.
  FXL_WARN_UNUSED_RESULT virtual Status GetByPrefix(
      coroutine::CoroutineHandler* handler, convert::ExtendedStringView prefix,
      std::vector<std::string>* key_suffixes) = 0;

  // Retrieves all entries matching the given |prefix|. The keys of the
  // returned entries will be updated not to contain the |prefix|.
  FXL_WARN_UNUSED_RESULT virtual Status GetEntriesByPrefix(
      coroutine::CoroutineHandler* handler, convert::ExtendedStringView prefix,
      std::vector<std::pair<std::string, std::string>>* entries) = 0;

  // Retrieves an entry iterator over the entries whose keys start with
  // |prefix|.
  FXL_WARN_UNUSED_RESULT virtual Status GetIteratorAtPrefix(
      coroutine::CoroutineHandler* handler, convert::ExtendedStringView prefix,
      std::unique_ptr<Iterator<const std::pair<convert::ExtendedStringView,
                                               convert::ExtendedStringView>>>*
          iterator) = 0;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_PUBLIC_DB_H_
