// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_LEVELDB_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_LEVELDB_H_

#include <lib/async/dispatcher.h>

#include <utility>

#include "leveldb/db.h"
#include "leveldb/write_batch.h"
#include "src/ledger/bin/platform/detached_path.h"
#include "src/ledger/bin/platform/platform.h"
#include "src/ledger/bin/storage/public/db.h"

namespace storage {

// Implementation of Db based on LevelDb.
//
// Note that the underlying LevelDb has a synchronous API, however the API this class needs to
// expose is asynchronous through coroutines. This class systematically suspends the current
// coroutine when entering each public function and posts a task to the message loop to resume it,
// so that the methods are effectively asynchronous.
class LevelDb : public Db {
 public:
  explicit LevelDb(ledger::FileSystem* file_system, async_dispatcher_t* dispatcher,
                   ledger::DetachedPath db_path);

  LevelDb(const LevelDb&) = delete;
  LevelDb& operator=(const LevelDb&) = delete;
  ~LevelDb() override;

  Status Init();

  // Db:
  Status StartBatch(coroutine::CoroutineHandler* handler, std::unique_ptr<Batch>* batch) override;
  Status Get(coroutine::CoroutineHandler* handler, convert::ExtendedStringView key,
             std::string* value) override;
  Status HasKey(coroutine::CoroutineHandler* handler, convert::ExtendedStringView key) override;
  Status HasPrefix(coroutine::CoroutineHandler* handler,
                   convert::ExtendedStringView prefix) override;
  Status GetObject(coroutine::CoroutineHandler* handler, convert::ExtendedStringView key,
                   ObjectIdentifier object_identifier,
                   std::unique_ptr<const Piece>* piece) override;
  Status GetByPrefix(coroutine::CoroutineHandler* handler, convert::ExtendedStringView prefix,
                     std::vector<std::string>* key_suffixes) override;
  Status GetEntriesByPrefix(coroutine::CoroutineHandler* handler,
                            convert::ExtendedStringView prefix,
                            std::vector<std::pair<std::string, std::string>>* entries) override;
  Status GetIteratorAtPrefix(
      coroutine::CoroutineHandler* handler, convert::ExtendedStringView prefix,
      std::unique_ptr<
          Iterator<const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>>>*
          iterator) override;

 private:
  ledger::FileSystem* file_system_;
  async_dispatcher_t* const dispatcher_;
  const ledger::DetachedPath db_path_;
  std::unique_ptr<leveldb::Env> env_;
  std::unique_ptr<leveldb::DB> db_;

  const leveldb::WriteOptions write_options_;
  const leveldb::ReadOptions read_options_;

  uint64_t active_batches_count_ = 0;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_LEVELDB_H_
