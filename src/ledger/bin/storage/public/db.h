// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_PUBLIC_DB_H_
#define SRC_LEDGER_BIN_STORAGE_PUBLIC_DB_H_

#include "src/ledger/bin/storage/public/iterator.h"
#include "src/ledger/bin/storage/public/object.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "third_party/abseil-cpp/absl/base/attributes.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {

// An interface abstracting access to a key-value database.
//
// All operations accessing the database are asynchronous, through the use of coroutines.
//
// Implementations of this interface do not need to guarantee that operations complete in order: two
// |Get| operations may return out of order, for instance.
//
// However, implementations of this database must ensure that operations are strictly consistent
// when issued from the same thread (but potentially different coroutines): reads and writes are not
// allowed to be reordered, ie. any write to the database must be seen by all coroutines performing
// subsequent reads, and by none which issued its read beforehand. Writes are considered issued at
// the time when |Batch::Execute| is called, and reads at the time when |Get| (or |HasKey| and other
// related methods) is called.
class Db {
 public:
  class Batch {
   public:
    // A |Batch| can be used to execute a number of updates in |Db| atomically.
    Batch() = default;
    Batch(const Batch&) = delete;
    Batch& operator=(const Batch&) = delete;
    virtual ~Batch() = default;

    // Inserts the given key-value pair in the database.
    ABSL_MUST_USE_RESULT virtual Status Put(coroutine::CoroutineHandler* handler,
                                            convert::ExtendedStringView key,
                                            absl::string_view value) = 0;

    // Deletes the entry in the database with the given |key|.
    ABSL_MUST_USE_RESULT virtual Status Delete(coroutine::CoroutineHandler* handler,
                                               convert::ExtendedStringView key) = 0;

    // Executes this batch. No further operations in this batch are supported
    // after a successful execution.
    ABSL_MUST_USE_RESULT virtual Status Execute(coroutine::CoroutineHandler* handler) = 0;
  };

  Db() = default;
  virtual ~Db() = default;

  // Starts a new batch. The batch will be written when Execute is called on the
  // returned object. The Db object must outlive the batch object.
  // The handler (and the corresponding coroutine) only need to remain active
  // until the result is returned. If the coroutine is interrupted,
  // |INTERRUPTED| status is returned.
  ABSL_MUST_USE_RESULT virtual Status StartBatch(coroutine::CoroutineHandler* handler,
                                                 std::unique_ptr<Batch>* batch) = 0;

  // Retrieves the value corresponding to |key|.
  ABSL_MUST_USE_RESULT virtual Status Get(coroutine::CoroutineHandler* handler,
                                          convert::ExtendedStringView key, std::string* value) = 0;

  // Checks whether |key| is stored in this database. Returns |OK| if the key
  // was found, |INTERNAL_NOT_FOUND| if not, or another type of error in case of
  // failure to look up.
  ABSL_MUST_USE_RESULT virtual Status HasKey(coroutine::CoroutineHandler* handler,
                                             convert::ExtendedStringView key) = 0;

  // Checks whether any key with the given |prefix| is stored in this database. Returns |OK| if the
  // prefix was found, |INTERNAL_NOT_FOUND| if not, or another type of error in case of failure to
  // look up.
  ABSL_MUST_USE_RESULT virtual Status HasPrefix(coroutine::CoroutineHandler* handler,
                                                convert::ExtendedStringView prefix) = 0;

  // Retrieves the value for the given |key| as a Piece with the provided
  // |object_identifier|.
  ABSL_MUST_USE_RESULT virtual Status GetObject(coroutine::CoroutineHandler* handler,
                                                convert::ExtendedStringView key,
                                                ObjectIdentifier object_identifier,
                                                std::unique_ptr<const Piece>* piece) = 0;

  // Retrieves all keys matching the given |prefix|. |key_suffixes| will be
  // updated to contain the suffixes of corresponding keys.
  ABSL_MUST_USE_RESULT virtual Status GetByPrefix(coroutine::CoroutineHandler* handler,
                                                  convert::ExtendedStringView prefix,
                                                  std::vector<std::string>* key_suffixes) = 0;

  // Retrieves all entries matching the given |prefix|. The keys of the
  // returned entries will be updated not to contain the |prefix|.
  ABSL_MUST_USE_RESULT virtual Status GetEntriesByPrefix(
      coroutine::CoroutineHandler* handler, convert::ExtendedStringView prefix,
      std::vector<std::pair<std::string, std::string>>* entries) = 0;

  // Retrieves an entry iterator over the entries whose keys start with
  // |prefix|.
  ABSL_MUST_USE_RESULT virtual Status GetIteratorAtPrefix(
      coroutine::CoroutineHandler* handler, convert::ExtendedStringView prefix,
      std::unique_ptr<
          Iterator<const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>>>*
          iterator) = 0;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_PUBLIC_DB_H_
