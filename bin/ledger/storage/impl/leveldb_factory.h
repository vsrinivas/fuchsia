// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_LEVELDB_FACTORY_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_LEVELDB_FACTORY_H_

#include "peridot/bin/ledger/storage/impl/db_factory.h"

#include <memory>

#include "peridot/bin/ledger/filesystem/detached_path.h"
#include "peridot/bin/ledger/storage/impl/leveldb.h"

namespace storage {

// A factory for LevelDb instances.
// TODO(LE-617): Update API to return pre-cached, initialized LevelDb instances.
class LevelDbFactory : public DbFactory {
 public:
  explicit LevelDbFactory(async_dispatcher_t* dispatcher);

  void CreateDb(
      ledger::DetachedPath db_path,
      fit::function<void(Status, std::unique_ptr<LevelDb>)> callback) override;

  void GetDb(
      ledger::DetachedPath db_path,
      fit::function<void(Status, std::unique_ptr<LevelDb>)> callback) override;

 private:
  async_dispatcher_t* dispatcher_;
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_LEVELDB_FACTORY_H_
