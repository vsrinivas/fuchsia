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
  LevelDbFactory(ledger::Environment* environment,
                 ledger::DetachedPath cache_path);

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
  enum class CreateInStagingPath : bool;

  // Creates a new instance of LevelDb in the given |db_path|, initializes it
  // in the I/O thread and then returns it through the |callback|.
  void CreateInitializedDb(
      ledger::DetachedPath db_path, CreateInStagingPath create_in_staging_path,
      fit::function<void(Status, std::unique_ptr<Db>)> callback);

  // Creates and initializes a new LevelDb instance. This method should be
  // called from the I/O thread. When initialization is complete, it makes sure
  // to call the |callback| with the computed result from the main thread.
  void InitOnIOThread(
      ledger::DetachedPath db_path, CreateInStagingPath create_in_staging_path,
      fxl::RefPtr<DbInitializationState> initialization_state,
      fit::function<void(Status, std::unique_ptr<Db>)> callback);

  // Synchronously creates and initializes a new LevelDb instance in a two-step
  // process: the new instance is created in a temporary directory under the
  // staging path and, if successful, it is then moved to the given |db_path|.
  // This way, if initialization is interrupted, the potentially corrupted
  // database will be in the staging area.
  Status CreateInitializedDbThroughStagingPath(ledger::DetachedPath db_path,
                                               std::unique_ptr<LevelDb>* db);

  ledger::Environment* environment_;
  // The path where new LevelDb instances are created, before they are moved to
  // their final destination.
  ledger::DetachedPath staging_path_;
  coroutine::CoroutineManager coroutine_manager_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LevelDbFactory);
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_LEVELDB_FACTORY_H_
