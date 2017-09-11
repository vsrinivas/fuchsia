// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_LEVELDB_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_LEVELDB_H_

#include "apps/ledger/src/storage/impl/db.h"

#include <utility>

#include "leveldb/db.h"
#include "leveldb/write_batch.h"

namespace storage {

class LevelDb : public Db {
 public:
  explicit LevelDb(std::string db_path);

  ~LevelDb() override;

  Status Init();

  // Db:
  std::unique_ptr<Batch> StartBatch() override;
  Status Get(convert::ExtendedStringView key, std::string* value) override;
  Status HasKey(convert::ExtendedStringView key, bool* has_key) override;
  Status GetObject(convert::ExtendedStringView key,
                   ObjectId object_id,
                   std::unique_ptr<const Object>* object) override;
  Status GetByPrefix(convert::ExtendedStringView prefix,
                     std::vector<std::string>* key_suffixes) override;
  Status GetEntriesByPrefix(
      convert::ExtendedStringView prefix,
      std::vector<std::pair<std::string, std::string>>* entries) override;
  Status GetIteratorAtPrefix(
      convert::ExtendedStringView prefix,
      std::unique_ptr<Iterator<const std::pair<convert::ExtendedStringView,
                                               convert::ExtendedStringView>>>*
          iterator) override;

 private:
  const std::string db_path_;
  std::unique_ptr<leveldb::DB> db_;

  const leveldb::WriteOptions write_options_;
  const leveldb::ReadOptions read_options_;

  uint64_t active_batches_count_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(LevelDb);
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_LEVELDB_H_
