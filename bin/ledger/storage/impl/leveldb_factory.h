// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_LEVELDB_FACTORY_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_LEVELDB_FACTORY_H_

#include "peridot/bin/ledger/storage/public/db_factory.h"

#include <memory>

#include "peridot/bin/ledger/coroutine/coroutine_manager.h"
#include "peridot/bin/ledger/environment/environment.h"
#include "peridot/bin/ledger/filesystem/detached_path.h"
#include "peridot/bin/ledger/storage/impl/leveldb.h"

namespace storage {

// A factory for LevelDb instances.
//
// When creating new LevelDb instances, using either |CreateDb| or |GetDb|, the
// caller should make sure that there is no live LevelDb instance for the same
// path.
class LevelDbFactory : public DbFactory {
 public:
  explicit LevelDbFactory(ledger::Environment* environment);

  // TODO(LE-617): Update implementation to return pre-cached, LevelDb
  // instances.
  void CreateDb(
      ledger::DetachedPath db_path,
      fit::function<void(Status, std::unique_ptr<Db>)> callback) override;

  void GetDb(
      ledger::DetachedPath db_path,
      fit::function<void(Status, std::unique_ptr<Db>)> callback) override;

 private:
  struct DbInitializationState;

  // Creates a new instance of LevelDb in the given |db_path|, initializes it
  // in the I/O thread and then returns it through the |callback|.
  void CreateInitializedDb(
      ledger::DetachedPath db_path,
      fit::function<void(Status, std::unique_ptr<Db>)> callback);

  // Creates and initializes a new LevelDb instance. This method should be
  // called from the I/O thread. When initialization is complete, it makes sure
  // to call the |callback| with the computed result from the main thread.
  void InitOnIOThread(
      ledger::DetachedPath db_path,
      fxl::RefPtr<DbInitializationState> initialization_state,
      fit::function<void(Status, std::unique_ptr<Db>)> callback);

  ledger::Environment* environment_;
  coroutine::CoroutineManager coroutine_manager_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LevelDbFactory);
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_LEVELDB_FACTORY_H_
