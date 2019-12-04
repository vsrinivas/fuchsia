// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_LEVELDB_FACTORY_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_LEVELDB_FACTORY_H_

#include <lib/async/cpp/executor.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>

#include <memory>

#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/platform/detached_path.h"
#include "src/ledger/bin/storage/impl/leveldb.h"
#include "src/ledger/bin/storage/public/db_factory.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace storage {

// A factory for LevelDb instances.
//
// This factory tries to always keep a new empty instance of LevelDb,
// initialized and pre-cached, in order to immediately respond to requests for
// new Db instances.
//
// When creating new LevelDb instances, using |GetOrCreateDb|, the caller should
// make sure that there is no live LevelDb instance for the same path.
class LevelDbFactory : public DbFactory {
 public:
  LevelDbFactory(ledger::Environment* environment, ledger::DetachedPath cache_path);

  LevelDbFactory(const LevelDbFactory&) = delete;
  LevelDbFactory& operator=(const LevelDbFactory&) = delete;
  ~LevelDbFactory() override;

  // Initializes the LevelDbFactory by preparing the cached instance of LevelDb.
  void Init();

  // DbFactory:
  void GetOrCreateDb(ledger::DetachedPath db_path, DbFactory::OnDbNotFound on_db_not_found,
                     fit::function<void(Status, std::unique_ptr<Db>)> callback) override;

 private:
  // IOLevelDbFactory holds all operations happening on the IO thread.
  class IOLevelDbFactory;

  bool initialized_;
  std::unique_ptr<IOLevelDbFactory> io_level_db_factory_;
  async::Executor main_executor_;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_LEVELDB_FACTORY_H_
