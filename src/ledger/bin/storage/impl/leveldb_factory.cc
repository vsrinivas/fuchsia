// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/leveldb_factory.h"

#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <lib/fit/result.h>
#include <lib/fit/scope.h>

#include <mutex>

#include <trace/event.h>

#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/callback/auto_cleanable.h"
#include "src/lib/callback/scoped_callback.h"
#include "src/lib/callback/trace_callback.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/memory/ref_counted.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

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
constexpr absl::string_view kStagingPath = "staging";
constexpr absl::string_view kCachedDbPath = "cached_db";

constexpr size_t kRandomBytesCount = 16;

// Returns whether the parent directory of |path| exists. If it is not possible
// to access the parent directory, returns whether the given |path| exists.
bool ParentDirectoryExists(ledger::FileSystem* file_system, ledger::DetachedPath path) {
  size_t last_slash = path.path().find_last_of('/');
  ledger::DetachedPath directory_path(path.root_fd(), last_slash == std::string::npos
                                                          ? path.path()
                                                          : path.path().substr(0, last_slash));
  return file_system->IsDirectory(directory_path);
}

enum class CreateInStagingPath : bool {
  NO,
  YES,
};

// LockingWrapper that allows to block the execution of wrapped promises.
class LockingWrapper {
 public:
  LockingWrapper() = default;

  // Wrapper implementation, as expected by fit::promise:
  template <class Promise>
  decltype(auto) wrap(Promise promise) {
    assert(promise);
    return fit::make_promise_with_continuation(
        LockingWrappedContinuation<Promise>(this, std::move(promise)));
  }

  // Acquire a lock on the promise execution, effectively blocking any wrapped
  // promise once acquired.
  std::unique_lock<std::mutex> lock() { return std::unique_lock<std::mutex>(mutex_); }

 private:
  // Promise continuation that acquires the shared lock from LockingWrapper
  // before executing the promise.
  template <class Promise>
  class LockingWrappedContinuation {
   public:
    explicit LockingWrappedContinuation(LockingWrapper* wrapper, Promise promise)
        : wrapper_(wrapper), promise_(std::move(promise)) {}

    // Executes the wrapped promise.
    typename Promise::result_type operator()(fit::context& context) {
      std::lock_guard<std::mutex> lg(wrapper_->mutex_);
      return promise_(context);
    }

   private:
    LockingWrapper* const wrapper_;
    Promise promise_;
  };

  std::mutex mutex_;
};

// ScopedExecutor is a proxy for async::Executor that ensures that all tasks
// scheduled on it can be stopped from another thread.
class ScopedAsyncExecutor {
 public:
  // Creates a ScopedAsyncExecutor using the provided async loop dispatcher.
  explicit ScopedAsyncExecutor(async_dispatcher_t* dispatcher) : executor_(dispatcher) {
    scope_.emplace();
  }

  ~ScopedAsyncExecutor() { FXL_DCHECK(stopped_); }

  // fit::executor:
  void schedule_task(fit::promise<> task) {
    if (stopped_) {
      return;
    }
    // The wrapping order is important: by putting the locking wrapper after the
    // scope wrapper, we ensure that tasks are first locked before we check for
    // the scope's destruction. Thus, each promise is wrapped twice:
    // LockingWrapper[fit::scope[promise]]
    // This way, when we want to stop the executor and acquire the lock, we are
    // sure that the scoped promises are not executing because they are still at
    // the locking step. Once the scope is deleted and the lock released, the
    // executor will try to execute the scoped promises, and exit early.
    executor_.schedule_task(task.wrap_with(*scope_).wrap_with(wrapper_));
  }

  // Stop the executor. Once this method returns, it is guaranteed that no code
  // provided to |schedule_task| will be executed. It is however unsafe to
  // delete the class object at this point if |Stop| has been called on a
  // different thread than the one used by this executor, as management code may
  // still be running.
  void Stop() {
    auto lock = wrapper_.lock();
    scope_.reset();
    stopped_ = true;
  }

 private:
  bool stopped_ = false;
  async::Executor executor_;
  std::optional<fit::scope> scope_;
  LockingWrapper wrapper_;
};
}  // namespace

// IOLevelDbFactory holds all operations happening on the IO thread.
class LevelDbFactory::IOLevelDbFactory {
 public:
  IOLevelDbFactory(ledger::Environment* environment, ledger::DetachedPath cache_path)
      : environment_(environment),
        staging_path_(cache_path.SubPath(kStagingPath)),
        cached_db_path_(cache_path.SubPath(kCachedDbPath)),
        io_executor_(environment_->io_dispatcher()) {}

  // Initialize the IO factory.
  void Init();

  // Returns through the completer a LevelDB database, initialized on the IO
  // thread.
  void GetOrCreateDb(ledger::DetachedPath db_path, DbFactory::OnDbNotFound on_db_not_found,
                     fit::completer<std::unique_ptr<Db>, Status> completer);

  // Self-destructs this class on the IO thread.
  // |io_executor_| can't be destroyed when a task is in progress, and
  // |io_executor_| tasks use member variables to operate. Thus, by scheduling
  // the deletion of this class on the same dispatcher as the |io_executor_|, we
  // ensure that |io_executor_| is destroyed when no task is running and that
  // no task will access member variables after their destruction.
  // This method will block the main thread while doing this.
  void SelfDestruct(std::unique_ptr<LevelDbFactory::IOLevelDbFactory> self);

 private:
  // Gets or creates a new LevelDb instance in the given |db_path|,
  // initializes and then returns it through the completer. Callers should
  // execute the returned promise, as it would contain any deferred
  // computation, if deferred computation is needed. This method should be
  // called on the I/O thread.
  fit::promise<> GetOrCreateDbOnIOThread(ledger::DetachedPath db_path,
                                         DbFactory::OnDbNotFound on_db_not_found,
                                         fit::completer<std::unique_ptr<Db>, Status> completer);

  // Gets or creates a new LevelDb instance.
  // This method should be called on the I/O thread.
  fit::result<std::unique_ptr<Db>, Status> GetOrCreateDbAtPathOnIOThread(
      ledger::DetachedPath db_path, CreateInStagingPath create_in_staging_path);

  // Synchronously creates and initializes a new LevelDb instance in a two-step
  // process: the new instance is created in a temporary directory under the
  // staging path and, if successful, it is then moved to the given |db_path|.
  // This way, if initialization is interrupted, the potentially corrupted
  // database will be in the staging area.
  // This method should be called on the I/O thread.
  Status CreateDbThroughStagingPathOnIOThread(ledger::DetachedPath db_path,
                                              std::unique_ptr<LevelDb>* db);

  // Synchronously creates a new cached DB in the cached db path.
  // This method should be called on the I/O thread.
  fit::result<std::unique_ptr<Db>, Status> PrepareCachedDbOnIOThread(
      CreateInStagingPath create_in_staging_path);

  // Sychronously prepares a precached DB for normal use.
  // This method should be called on the I/O thread.
  fit::result<std::unique_ptr<Db>, Status> ReturnPrecachedDbOnIOThread(
      ledger::DetachedPath db_path, fit::result<std::unique_ptr<Db>, Status> result);

  // We hold a cached database to speed up initialization. |cached_db_| is only
  // manipulated on the I/O thread.
  fit::future<std::unique_ptr<Db>, Status> cached_db_;

  ledger::Environment* const environment_;
  // The path where new LevelDb instances are created, before they are moved to
  // their final destination, or the cached db path.
  const ledger::DetachedPath staging_path_;
  // The path that keeps the initialized cached instance of LevelDb.
  const ledger::DetachedPath cached_db_path_;
  ScopedAsyncExecutor io_executor_;
};

void LevelDbFactory::IOLevelDbFactory::Init() {
  // If there is already a LevelDb instance in the cache directory, initialize
  // that one, instead of creating a new one.
  io_executor_.schedule_task(fit::make_promise([this](fit::context& context) {
    fit::bridge<std::unique_ptr<Db>, Status> bridge;
    cached_db_ = bridge.consumer.promise();
    CreateInStagingPath create_in_staging_path = static_cast<CreateInStagingPath>(
        !environment_->file_system()->IsDirectory(cached_db_path_));
    auto cache_db_result = PrepareCachedDbOnIOThread(create_in_staging_path);
    bridge.completer.complete_or_abandon(std::move(cache_db_result));
  }));
}

void LevelDbFactory::IOLevelDbFactory::GetOrCreateDb(
    ledger::DetachedPath db_path, DbFactory::OnDbNotFound on_db_not_found,
    fit::completer<std::unique_ptr<Db>, Status> completer) {
  io_executor_.schedule_task(
      fit::make_promise([this, db_path, on_db_not_found,
                         completer = std::move(completer)](fit::context& context) mutable {
        return GetOrCreateDbOnIOThread(db_path, on_db_not_found, std::move(completer));
      }));
}

void LevelDbFactory::IOLevelDbFactory::SelfDestruct(
    std::unique_ptr<LevelDbFactory::IOLevelDbFactory> self) {
  FXL_DCHECK(self.get() == this);
  io_executor_.Stop();
  std::unique_ptr<ledger::Notification> notification = environment_->MakeNotification();
  async::PostTask(environment_->io_dispatcher(),
                  [self = std::move(self), notification = notification.get()]() mutable {
                    self.reset();
                    notification->Notify();
                  });
  notification->WaitForNotification();
}

fit::promise<> LevelDbFactory::IOLevelDbFactory::GetOrCreateDbOnIOThread(
    ledger::DetachedPath db_path, DbFactory::OnDbNotFound on_db_not_found,
    fit::completer<std::unique_ptr<Db>, Status> completer) {
  if (environment_->file_system()->IsDirectory(db_path)) {
    // If the path exists, there is a LevelDb instance already there. Open and
    // return it.
    auto result = GetOrCreateDbAtPathOnIOThread(std::move(db_path), CreateInStagingPath::NO);
    completer.complete_or_abandon(std::move(result));
    return fit::promise<>();
  }

  if (on_db_not_found == DbFactory::OnDbNotFound::RETURN) {
    completer.complete_or_abandon(fit::error(Status::PAGE_NOT_FOUND));
    return fit::promise<>();
  }

  switch (cached_db_.state()) {
    case fit::future_state::ok:
      completer.complete_or_abandon(
          ReturnPrecachedDbOnIOThread(std::move(db_path), cached_db_.take_result()));
      return fit::promise<>();
    case fit::future_state::pending:
      return cached_db_.take_promise().then(
          [this, db_path, completer = std::move(completer)](
              fit::result<std::unique_ptr<Db>, Status>& cache_result) mutable -> void {
            completer.complete_or_abandon(
                ReturnPrecachedDbOnIOThread(std::move(db_path), std::move(cache_result)));
          });
    case fit::future_state::empty:
      // If creating the pre-cached db failed at some point it will likely fail
      // again. Don't retry caching anymore.
    case fit::future_state::error: {
      // Either creation of a cached db has failed or a previous request is
      // already waiting for the cached instance. Request a new LevelDb at the
      // final destination.
      completer.complete_or_abandon(
          GetOrCreateDbAtPathOnIOThread(std::move(db_path), CreateInStagingPath::YES));
      return fit::promise<>();
    }
  }
}

fit::result<std::unique_ptr<Db>, Status>
LevelDbFactory::IOLevelDbFactory::GetOrCreateDbAtPathOnIOThread(
    ledger::DetachedPath db_path, CreateInStagingPath create_in_staging_path) {
  TRACE_DURATION("ledger", "new_db_creation");
  std::unique_ptr<LevelDb> leveldb;
  Status status;
  if (create_in_staging_path == CreateInStagingPath::YES) {
    status = CreateDbThroughStagingPathOnIOThread(std::move(db_path), &leveldb);
  } else {
    FXL_DCHECK(environment_->file_system()->IsDirectory(db_path));
    leveldb = std::make_unique<LevelDb>(environment_->file_system(), environment_->dispatcher(),
                                        std::move(db_path));
    status = leveldb->Init();
  }
  if (status != Status::OK) {
    return fit::error(status);
  }
  return fit::ok(std::move(leveldb));
}

Status LevelDbFactory::IOLevelDbFactory::CreateDbThroughStagingPathOnIOThread(
    ledger::DetachedPath db_path, std::unique_ptr<LevelDb>* db) {
  char name[kRandomBytesCount];
  environment_->random()->Draw(name, kRandomBytesCount);
  ledger::DetachedPath tmp_destination =
      staging_path_.SubPath(convert::ToHex(absl::string_view(name, kRandomBytesCount)));
  // Create a LevelDb instance in a temporary path.
  auto result = std::make_unique<LevelDb>(environment_->file_system(), environment_->dispatcher(),
                                          tmp_destination);
  RETURN_ON_ERROR(result->Init());
  // If the parent directory doesn't exist, renameat will fail.
  // Note that |cached_db_path_| will also be created throught the staging path
  // and thus, this code path will be reached. Its parent directory is lazily
  // created when result->Init() (see code above) is called:
  // - |staging_path_| and |cached_db_path_| share the same parent (the
  //   |cache_path| given on the constructor), and
  // - in LevelDb initialization, the directories up to the db path are created.
  FXL_DCHECK(ParentDirectoryExists(environment_->file_system(), db_path))
      << "Parent directory does not exit for path: " << db_path.path();
  // Move it to the final destination.
  if (renameat(tmp_destination.root_fd(), tmp_destination.path().c_str(), db_path.root_fd(),
               db_path.path().c_str()) != 0) {
    FXL_LOG(ERROR) << "Unable to move LevelDb from staging path to final "
                      "destination: "
                   << db_path.path() << ". Error: " << strerror(errno);
    return Status::IO_ERROR;
  }
  *db = std::move(result);
  return Status::OK;
}

fit::result<std::unique_ptr<Db>, Status>
LevelDbFactory::IOLevelDbFactory::PrepareCachedDbOnIOThread(
    CreateInStagingPath create_in_staging_path) {
  TRACE_DURATION("ledger", "prepare_cached_db");
  return GetOrCreateDbAtPathOnIOThread(cached_db_path_, create_in_staging_path);
}

fit::result<std::unique_ptr<Db>, Status>
LevelDbFactory::IOLevelDbFactory::ReturnPrecachedDbOnIOThread(
    ledger::DetachedPath db_path, fit::result<std::unique_ptr<Db>, Status> result) {
  if (result.is_error()) {
    // If we failed to create a cached db instance, any future attempts will
    // likely fail as well: just return the error, and subsequent attempts will
    // not attempt to use a cached DB.
    return result;
  }

  // Move the cached db to the final destination.
  if (renameat(cached_db_path_.root_fd(), cached_db_path_.path().c_str(), db_path.root_fd(),
               db_path.path().c_str()) != 0) {
    FXL_LOG(ERROR) << "Unable to move LevelDb from: " << cached_db_path_.path()
                   << " to final destination: " << db_path.path() << ". Error: " << strerror(errno);
    // Moving to the final destination failed, but the cached db was created
    // succesfully: we fail, and we'll retry the cached db next time.
    fit::bridge<std::unique_ptr<Db>, Status> bridge;
    cached_db_ = bridge.consumer.promise();
    bridge.completer.complete_or_abandon(std::move(result));
    return fit::error(Status::IO_ERROR);
  }

  // Asynchronously start preparing the next cached db.
  fit::bridge<std::unique_ptr<Db>, Status> bridge;
  cached_db_ = bridge.consumer.promise();
  io_executor_.schedule_task(fit::make_promise(
      [this, completer = std::move(bridge.completer)](fit::context& context) mutable {
        auto cache_db_result = PrepareCachedDbOnIOThread(CreateInStagingPath::YES);
        completer.complete_or_abandon(std::move(cache_db_result));
      }));
  return result;
}

LevelDbFactory::LevelDbFactory(ledger::Environment* environment, ledger::DetachedPath cache_path)
    : initialized_(false), main_executor_(environment->dispatcher()) {
  io_level_db_factory_ = std::make_unique<IOLevelDbFactory>(environment, cache_path);
}

LevelDbFactory::~LevelDbFactory() {
  FXL_DCHECK(initialized_);
  io_level_db_factory_->SelfDestruct(std::move(io_level_db_factory_));
}

void LevelDbFactory::Init() {
  FXL_DCHECK(!initialized_);
  io_level_db_factory_->Init();
  initialized_ = true;
}

void LevelDbFactory::GetOrCreateDb(ledger::DetachedPath db_path,
                                   DbFactory::OnDbNotFound on_db_not_found,
                                   fit::function<void(Status, std::unique_ptr<Db>)> callback) {
  if (!initialized_) {
    callback(Status::ILLEGAL_STATE, nullptr);
    return;
  }
  fit::bridge<std::unique_ptr<Db>, Status> bridge;
  io_level_db_factory_->GetOrCreateDb(db_path, on_db_not_found, std::move(bridge.completer));

  main_executor_.schedule_task(
      bridge.consumer.promise_or(fit::error(Status::ILLEGAL_STATE))
          .then([callback = std::move(callback)](fit::result<std::unique_ptr<Db>, Status>& result) {
            switch (result.state()) {
              case fit::result_state::error:
                callback(result.take_error(), nullptr);
                return;
              case fit::result_state::ok:
                callback(Status::OK, result.take_value());
                return;
              case fit::result_state::pending:
                FXL_NOTREACHED();
                return;
            }
          }));
}

}  // namespace storage
