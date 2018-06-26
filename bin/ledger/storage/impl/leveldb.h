// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_LEVELDB_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_LEVELDB_H_

#include <utility>

#include <lib/async/dispatcher.h>

#include "leveldb/db.h"
#include "leveldb/write_batch.h"
#include "peridot/bin/ledger/filesystem/detached_path.h"
#include "peridot/bin/ledger/storage/impl/db.h"

namespace storage {

class LevelDb : public Db {
 public:
  explicit LevelDb(async_t* async, ledger::DetachedPath db_path);

  ~LevelDb() override;

  Status Init();

  // Db:
  Status StartBatch(coroutine::CoroutineHandler* handler,
                    std::unique_ptr<Batch>* batch) override;
  Status Get(coroutine::CoroutineHandler* handler,
             convert::ExtendedStringView key, std::string* value) override;
  Status HasKey(coroutine::CoroutineHandler* handler,
                convert::ExtendedStringView key, bool* has_key) override;
  Status GetObject(coroutine::CoroutineHandler* handler,
                   convert::ExtendedStringView key,
                   ObjectIdentifier object_identifier,
                   std::unique_ptr<const Object>* object) override;
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
  async_t* const async_;
  const ledger::DetachedPath db_path_;
  std::unique_ptr<leveldb::Env> env_;
  std::unique_ptr<leveldb::DB> db_;

  const leveldb::WriteOptions write_options_;
  const leveldb::ReadOptions read_options_;

  uint64_t active_batches_count_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(LevelDb);
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_LEVELDB_H_
