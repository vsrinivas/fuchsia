// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/leveldb_factory.h"

#include <mutex>

#include <lib/async/cpp/task.h>
#include <lib/callback/scoped_callback.h>
#include <lib/callback/trace_callback.h>
#include <src/lib/fxl/memory/ref_counted.h>
#include <src/lib/fxl/memory/ref_ptr.h>
#include <src/lib/fxl/strings/string_view.h>
#include <src/lib/fxl/synchronization/thread_annotations.h>

#include "peridot/lib/convert/convert.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/scoped_temp_dir.h"

// LevelDbFactory tries to keep an empty, initialized instance of LevelDb always
// available. It stores this cached instance under cached_db/.
//
// On requests for new LevelDb instances (see |GetOrCreateDb|), if the cached
// instance is ready, it is moved to the requested destination and then a new
// LevelDb is prepared to be cached. If the cached instance is not yet
// available, the request is queued, and will be handled when the cached db is
// ready.
//
// Note that if multiple requests are received while waiting for the LevelDb
// initialization, only the first one is queued up. The rest directly request a
// new LevelDb instance at the final destination.

namespace storage {
namespace {

// TODO(LE-635): We need to clean the staging path, so that we don't leave
// unreachable storage on disk.
constexpr fxl::StringView kStagingPath = "staging";
constexpr fxl::StringView kCachedDbPath = "cached_db";

constexpr size_t kRandomBytesCount = 16;

// Returns whether the parent directory of |path| exists. If it is not possible
// to access the parent directory, returns whether the given |path| exists.
bool ParentDirectoryExists(ledger::DetachedPath path) {
  size_t last_slash = path.path().find_last_of('/');
  return files::IsDirectoryAt(path.root_fd(),
                              last_slash == std::string::npos
                                  ? path.path()
                                  : path.path().substr(0, last_slash));
}

}  // namespace

enum class LevelDbFactory::CreateInStagingPath : bool {
  NO,
  YES,
};

// Holds the LevelDb object together with the information on its initialization
// state, allowing the coordination between the main and the I/O thread for
// the creation of new LevelDb objects.
struct LevelDbFactory::DbWithInitializationState
    : public fxl::RefCountedThreadSafe<
          LevelDbFactory::DbWithInitializationState> {
 public:
  // The mutex used to avoid concurrency issues during initialization.
  std::mutex mutex;

  // Whether the initialization has been cancelled. This information is known on
  // the main thread, which is the only one that should update this field if
  // needed. The I/O thread should read |cancelled| to know whether to proceed
  // with completing the requested initialization.
  bool cancelled FXL_GUARDED_BY(mutex) = false;

  // The LevelDb object itself should only be initialized while holding the
  // mutex, to prevent a race condition when cancelling from the main thread.
  std::unique_ptr<LevelDb> db FXL_GUARDED_BY(mutex) = nullptr;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(DbWithInitializationState);
  FRIEND_MAKE_REF_COUNTED(DbWithInitializationState);

  DbWithInitializationState() {}
  ~DbWithInitializationState() {}
};

LevelDbFactory::LevelDbFactory(ledger::Environment* environment,
                               ledger::DetachedPath cache_path)
    : environment_(environment),
      staging_path_(cache_path.SubPath(kStagingPath)),
      cached_db_path_(cache_path.SubPath(kCachedDbPath)),
      coroutine_manager_(environment->coroutine_service()),
      weak_factory_(this) {}

void LevelDbFactory::Init() {
  // If there is already a LevelDb instance in the cache directory, initialize
  // that one, instead of creating a new one.
  CreateInStagingPath create_in_staging_path = static_cast<CreateInStagingPath>(
      !files::IsDirectoryAt(cached_db_path_.root_fd(), cached_db_path_.path()));
  PrepareCachedDb(create_in_staging_path);
}

void LevelDbFactory::GetOrCreateDb(
    ledger::DetachedPath db_path, DbFactory::OnDbNotFound on_db_not_found,
    fit::function<void(Status, std::unique_ptr<Db>)> callback) {
  if (files::IsDirectoryAt(db_path.root_fd(), db_path.path())) {
    // If the path exists, there is a LevelDb instance already there. Open and
    // return it.
    GetOrCreateDbAtPath(std::move(db_path), CreateInStagingPath::NO,
                        std::move(callback));
    return;
  }
  if (on_db_not_found == DbFactory::OnDbNotFound::RETURN) {
    callback(Status::PAGE_NOT_FOUND, nullptr);
    return;
  }
  // If creating the pre-cached db failed at some point it will likely fail
  // again. Don't retry caching anymore.
  if (cached_db_status_ == Status::OK) {
    if (cached_db_is_ready_) {
      // A cached instance is available. Use that one for the given callback.
      ReturnPrecachedDb(std::move(db_path), std::move(callback));
      return;
    }
    if (!pending_request_) {
      // The cached instance is not ready yet, and there are no other pending
      // requests. Store this one as pending until the cached db is ready.
      pending_request_path_ = std::move(db_path);
      pending_request_ = std::move(callback);
      return;
    }
  }
  // Either creation of a cached db has failed or a previous request is already
  // waiting for the cached instance. Request a new LevelDb at the final
  // destination.
  GetOrCreateDbAtPath(std::move(db_path), CreateInStagingPath::YES,
                      std::move(callback));
}

void LevelDbFactory::GetOrCreateDbAtPath(
    ledger::DetachedPath db_path, CreateInStagingPath create_in_staging_path,
    fit::function<void(Status, std::unique_ptr<Db>)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, db_path = std::move(db_path), create_in_staging_path](
          coroutine::CoroutineHandler* handler,
          fit::function<void(Status, std::unique_ptr<Db>)> callback) {
        auto db_with_initialization_state =
            fxl::MakeRefCounted<DbWithInitializationState>();
        Status status;
        if (coroutine::SyncCall(
                handler,
                [&](fit::function<void(Status)> callback) {
                  async::PostTask(
                      environment_->io_dispatcher(),
                      [this, db_path = std::move(db_path),
                       create_in_staging_path, db_with_initialization_state,
                       callback = std::move(callback)]() mutable {
                        GetOrCreateDbAtPathOnIOThread(
                            std::move(db_path), create_in_staging_path,
                            std::move(db_with_initialization_state),
                            std::move(callback));
                      });
                },
                &status) == coroutine::ContinuationStatus::OK) {
          // The coroutine returned normally, the initialization was done
          // completely on the I/O thread, return normally.
          std::lock_guard<std::mutex> guard(
              db_with_initialization_state->mutex);
          callback(status, std::move(db_with_initialization_state->db));
          return;
        }
        // The coroutine is interrupted, but the initialization has been posted
        // on the I/O thread. The lock must be acquired and |cancelled| must be
        // set to |true|.
        //
        // There are 3 cases to consider:
        // 1. The lock is acquired before |GetOrCreateDbAtPathOnIOThread| is
        //    called. |cancelled| will be set to |true| and when
        //    |GetOrCreateDbAtPathOnIOThread| is executed, it will return early.
        // 2. The lock is acquired after |GetOrCreateDbAtPathOnIOThread| is
        //    executed. |GetOrCreateDbAtPathOnIOThread| will not be called
        //    again, and there is no concurrency issue anymore.
        // 3. The lock is tentatively acquired while
        //    |GetOrCreateDbAtPathOnIOThread| is run. Because
        //    |GetOrCreateDbAtPathOnIOThread| is guarded by the same mutex, this
        //    will block until |GetOrCreateDbAtPathOnIOThread| is executed, and
        //    the case is the same as 2.

        std::lock_guard<std::mutex> guard(db_with_initialization_state->mutex);
        db_with_initialization_state->cancelled = true;
        db_with_initialization_state->db.reset();
        callback(Status::INTERRUPTED, nullptr);
      });
}

void LevelDbFactory::GetOrCreateDbAtPathOnIOThread(
    ledger::DetachedPath db_path, CreateInStagingPath create_in_staging_path,
    fxl::RefPtr<DbWithInitializationState> db_with_initialization_state,
    fit::function<void(Status)> callback) {
  std::lock_guard<std::mutex> guard(db_with_initialization_state->mutex);
  if (db_with_initialization_state->cancelled) {
    return;
  }
  Status status;
  if (create_in_staging_path == CreateInStagingPath::YES) {
    status = CreateDbThroughStagingPathOnIOThread(
        std::move(db_path), &db_with_initialization_state->db);
  } else {
    FXL_DCHECK(files::IsDirectoryAt(db_path.root_fd(), db_path.path()));
    db_with_initialization_state->db = std::make_unique<LevelDb>(
        environment_->dispatcher(), std::move(db_path));
    status = db_with_initialization_state->db->Init();
  }
  if (status != Status::OK) {
    // Don't return the created db instance if initialization failed.
    db_with_initialization_state->db.reset();
  }
  async::PostTask(
      environment_->dispatcher(),
      [status, callback = std::move(callback)]() mutable { callback(status); });
}

Status LevelDbFactory::CreateDbThroughStagingPathOnIOThread(
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
  // If the parent directory doesn't exist, renameat will fail.
  // Note that |cached_db_path_| will also be created throught the staging path
  // and thus, this code path will be reached. Its parent directory is lazily
  // created when result->Init() (see code above) is called:
  // - |staging_path_| and |cached_db_path_| share the same parent (the
  //   |cache_path| given on the constructor), and
  // - in LevelDb initialization, the directories up to the db path are created.
  FXL_DCHECK(ParentDirectoryExists(db_path))
      << "Parent directory does not exit for path: " << db_path.path();
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

void LevelDbFactory::PrepareCachedDb(
    CreateInStagingPath create_in_staging_path) {
  TRACE_ASYNC_BEGIN("ledger", "prepare_cached_db", 0);
  FXL_DCHECK(!cached_db_is_ready_);
  FXL_DCHECK(cached_db_ == nullptr);
  GetOrCreateDbAtPath(
      cached_db_path_, create_in_staging_path,
      callback::MakeScoped(weak_factory_.GetWeakPtr(),
                           [this](Status status, std::unique_ptr<Db> db) {
                             TRACE_ASYNC_END("ledger", "prepare_cached_db", 0);
                             cached_db_status_ = status;
                             cached_db_ = std::move(db);
                             cached_db_is_ready_ = true;
                             if (pending_request_) {
                               auto path = std::move(pending_request_path_);
                               auto callback = std::move(pending_request_);
                               pending_request_ = nullptr;
                               ReturnPrecachedDb(std::move(path),
                                                 std::move(callback));
                             }
                           }));
}

void LevelDbFactory::ReturnPrecachedDb(
    ledger::DetachedPath db_path,
    fit::function<void(Status, std::unique_ptr<Db>)> callback) {
  FXL_DCHECK(cached_db_is_ready_);

  if (cached_db_status_ != Status::OK) {
    // If we failed to create a cached db instance, any future attempts will
    // likely fail as well: just return the status and don't update
    // |cached_db_is_ready_| or call |PrepareCachedDb|.
    callback(cached_db_status_, nullptr);
    return;
  }
  // Move the cached db to the final destination.
  if (renameat(cached_db_path_.root_fd(), cached_db_path_.path().c_str(),
               db_path.root_fd(), db_path.path().c_str()) != 0) {
    FXL_LOG(ERROR) << "Unable to move LevelDb from: " << cached_db_path_.path()
                   << " to final destination: " << db_path.path()
                   << ". Error: " << strerror(errno);
    // Moving to the final destination failed, but the cached db was created
    // succesfully: no need to update |cached_db_is_ready_|, |cached_db_status_|
    // or |cached_db_|.
    callback(Status::IO_ERROR, nullptr);
    return;
  }

  // We know the |cached_db_status_| is |OK| and the db is already in the final
  // destination. Asynchronously start preparing the next cached db and then
  // call the callback.
  auto cached_db = std::move(cached_db_);
  cached_db_is_ready_ = false;
  PrepareCachedDb(CreateInStagingPath::YES);
  callback(Status::OK, std::move(cached_db));
}

}  // namespace storage
