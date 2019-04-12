// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_LEVELDB_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_LEVELDB_H_

#include <lib/async/dispatcher.h>

#include <utility>

#include "leveldb/db.h"
#include "leveldb/write_batch.h"
#include "src/ledger/bin/filesystem/detached_path.h"
#include "src/ledger/bin/storage/public/db.h"

namespace storage {

class LevelDb : public Db {
 public:
  explicit LevelDb(async_dispatcher_t* dispatcher,
                   ledger::DetachedPath db_path);

  ~LevelDb() override;

  Status Init();

  // Db:
  Status StartBatch(coroutine::CoroutineHandler* handler,
                    std::unique_ptr<Batch>* batch) override;
  Status Get(coroutine::CoroutineHandler* handler,
             convert::ExtendedStringView key, std::string* value) override;
  Status HasKey(coroutine::CoroutineHandler* handler,
                convert::ExtendedStringView key) override;
  Status GetObject(coroutine::CoroutineHandler* handler,
                   convert::ExtendedStringView key,
                   ObjectIdentifier object_identifier,
                   std::unique_ptr<const Piece>* piece) override;
  Status GetByPrefix(coroutine::CoroutineHandler* handler,
                     convert::ExtendedStringView prefix,
                     std::vector<std::string>* key_suffixes) override;
  Status GetEntriesByPrefix(
      coroutine::CoroutineHandler* handler, convert::ExtendedStringView prefix,
      std::vector<std::pair<std::string, std::string>>* entries) override;
  Status GetIteratorAtPrefix(
      coroutine::CoroutineHandler* handler, convert::ExtendedStringView prefix,
      std::unique_ptr<Iterator<const std::pair<convert::ExtendedStringView,
                                               convert::ExtendedStringView>>>*
          iterator) override;

 private:
  async_dispatcher_t* const dispatcher_;
  const ledger::DetachedPath db_path_;
  std::unique_ptr<leveldb::Env> env_;
  std::unique_ptr<leveldb::DB> db_;

  const leveldb::WriteOptions write_options_;
  const leveldb::ReadOptions read_options_;

  uint64_t active_batches_count_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(LevelDb);
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_LEVELDB_H_
