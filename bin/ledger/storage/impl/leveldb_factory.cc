// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/leveldb_factory.h"

#include <lib/async/cpp/task.h>
#include <lib/fxl/files/directory.h>
#include <lib/fxl/files/scoped_temp_dir.h>
#include <lib/fxl/memory/ref_counted.h>
#include <lib/fxl/memory/ref_ptr.h>
#include <lib/fxl/strings/string_view.h>

#include "peridot/lib/convert/convert.h"

namespace storage {
namespace {

// TODO(LE-635): We need to clean the staging path, so that we don't leave
// unreachable storage on disk.
constexpr fxl::StringView kStagingPath = "staging";

constexpr size_t kRandomBytesCount = 16;

}  // namespace

enum class LevelDbFactory::CreateInStagingPath : bool {
  YES,
  NO,
};

// Holds information on the initialization state of the LevelDb object, allowing
// the coordination between the main and the I/O thread for the creation of new
// LevelDb objects.
struct LevelDbFactory::DbInitializationState
    : public fxl::RefCountedThreadSafe<LevelDbFactory::DbInitializationState> {
 public:
  // Whether the initialization has been cancelled. This information is known on
  // the main thread, which is the only one that should update this field if
  // needed. The I/O thread should read |cancelled| to know whether to proceed
  // with completing the requested initialization.
  bool cancelled = false;

  // The mutex used to avoid concurrency issues during initialization.
  std::mutex mutex;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(DbInitializationState);
  FRIEND_MAKE_REF_COUNTED(DbInitializationState);

  DbInitializationState() {}
  ~DbInitializationState() {}
};

LevelDbFactory::LevelDbFactory(ledger::Environment* environment,
                               ledger::DetachedPath cache_path)
    : environment_(environment),
      staging_path_(cache_path.SubPath(kStagingPath)),
      coroutine_manager_(environment->coroutine_service()) {}

void LevelDbFactory::CreateDb(
    ledger::DetachedPath db_path,
    fit::function<void(Status, std::unique_ptr<Db>)> callback) {
  CreateInitializedDb(std::move(db_path), CreateInStagingPath::YES,
                      std::move(callback));
}

void LevelDbFactory::GetDb(
    ledger::DetachedPath db_path,
    fit::function<void(Status, std::unique_ptr<Db>)> callback) {
  // TODO(nellyv): Merge GetDb and CreateDb to GetOrCreateDb.
  CreateInStagingPath create_in_staging_path =
      files::IsDirectoryAt(db_path.root_fd(), db_path.path())
          ? CreateInStagingPath::NO
          : CreateInStagingPath::YES;
  CreateInitializedDb(std::move(db_path), create_in_staging_path,
                      std::move(callback));
}

void LevelDbFactory::CreateInitializedDb(
    ledger::DetachedPath db_path, CreateInStagingPath create_in_staging_path,
    fit::function<void(Status, std::unique_ptr<Db>)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, db_path = std::move(db_path), create_in_staging_path](
          coroutine::CoroutineHandler* handler,
          fit::function<void(Status, std::unique_ptr<Db>)> callback) {
        auto db_initialization_state =
            fxl::MakeRefCounted<DbInitializationState>();
        Status status;
        std::unique_ptr<Db> db;
        if (coroutine::SyncCall(
                handler,
                [&](fit::function<void(Status, std::unique_ptr<Db>)> callback) {
                  async::PostTask(
                      environment_->io_dispatcher(),
                      [this, db_path = std::move(db_path),
                       create_in_staging_path, db_initialization_state,
                       callback = std::move(callback)]() mutable {
                        InitOnIOThread(std::move(db_path),
                                       create_in_staging_path,
                                       std::move(db_initialization_state),
                                       std::move(callback));
                      });
                },
                &status, &db) == coroutine::ContinuationStatus::OK) {
          // The coroutine returned normally, the initialization was done
          // completely on the IO thread, return normally.
          callback(status, std::move(db));
          return;
        }
        // The coroutine is interrupted, but the initialization has been posted
        // on the io thread. The lock must be acquired and |cancelled| must be
        // set to |true|.
        //
        // There are 3 cases to consider:
        // 1. The lock is acquired before |InitOnIOThread| is called.
        //    |cancelled| will be set to |true| and when |InitOnIOThread| is
        //    executed, it will return early.
        // 2. The lock is acquired after |InitOnIOThread| is executed.
        //    |InitOnIOThread| will not be called again, and there is no
        //    concurrency issue anymore.
        // 3. The lock is tentatively acquired while |InitOnIOThread| is run.
        //    Because |InitOnIOThread| is guarded by the same mutex, this will
        //    block until |InitOnIOThread| is executed, and the case is the same
        //    as 2.

        std::lock_guard<std::mutex> guard(db_initialization_state->mutex);
        db_initialization_state->cancelled = true;
        callback(Status::INTERRUPTED, std::move(db));
      });
}

void LevelDbFactory::InitOnIOThread(
    ledger::DetachedPath db_path, CreateInStagingPath create_in_staging_path,
    fxl::RefPtr<DbInitializationState> initialization_state,
    fit::function<void(Status, std::unique_ptr<Db>)> callback) {
  std::lock_guard<std::mutex> guard(initialization_state->mutex);
  if (initialization_state->cancelled) {
    return;
  }
  Status status;
  std::unique_ptr<LevelDb> db;
  if (create_in_staging_path == CreateInStagingPath::YES) {
    status = CreateInitializedDbThroughStagingPath(std::move(db_path), &db);
  } else {
    FXL_DCHECK(files::IsDirectoryAt(db_path.root_fd(), db_path.path()));
    db = std::make_unique<LevelDb>(environment_->dispatcher(),
                                   std::move(db_path));
    status = db->Init();
  }
  async::PostTask(
      environment_->dispatcher(),
      [status, db = std::move(db), callback = std::move(callback)]() mutable {
        if (status != Status::OK) {
          // Don't return the created db instance if initialization failed.
          callback(status, nullptr);
          return;
        }
        callback(Status::OK, std::move(db));
      });
}

Status LevelDbFactory::CreateInitializedDbThroughStagingPath(
    ledger::DetachedPath db_path, std::unique_ptr<LevelDb>* db) {
  char name[kRandomBytesCount];
  environment_->random()->Draw(name, kRandomBytesCount);
  ledger::DetachedPath tmp_destination = staging_path_.SubPath(
      convert::ToHex(fxl::StringView(name, kRandomBytesCount)));
  // Create a LevelDb instance in a temporary path.
  auto result =
      std::make_unique<LevelDb>(environment_->dispatcher(), tmp_destination);
  Status status = result->Init();
  if (status != Status::OK) {
    return status;
  }
  // Move it to the final destination.
  if (renameat(tmp_destination.root_fd(), tmp_destination.path().c_str(),
               db_path.root_fd(), db_path.path().c_str()) != 0) {
    FXL_LOG(ERROR) << "Unable to move LevelDb from staging path to final "
                      "destination: "
                   << db_path.path() << ". Error: " << strerror(errno);
    return Status::IO_ERROR;
  }
  *db = std::move(result);
  return Status::OK;
}

}  // namespace storage
