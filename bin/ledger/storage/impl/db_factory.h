// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_DB_FACTORY_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_DB_FACTORY_H_

#include <memory>

#include "peridot/bin/ledger/filesystem/detached_path.h"
#include "peridot/bin/ledger/storage/impl/leveldb.h"

namespace storage {

// A factory for Db instances.
class DbFactory {
 public:
  DbFactory() {}
  virtual ~DbFactory() {}

  // Creates a new instance of LevelDb in the given |db_path|.
  // TODO(nellyv): Change LevelDb to Db.
  virtual void CreateDb(
      ledger::DetachedPath db_path,
      fit::function<void(Status, std::unique_ptr<LevelDb>)> callback) = 0;

  // Opens and returns an existing instance of LevelDb in the given |db_path|.
  // TODO(nellyv): Change LevelDb to Db.
  virtual void GetDb(
      ledger::DetachedPath db_path,
      fit::function<void(Status, std::unique_ptr<LevelDb>)> callback) = 0;
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_DB_FACTORY_H_
