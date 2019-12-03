// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/leveldb_factory.h"

#include <lib/async/cpp/task.h>

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/filesystem/detached_path.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/path.h"
#include "third_party/abseil-cpp/absl/strings/str_cat.h"

namespace storage {
namespace {

using testing::AnyOf;

class LevelDbFactoryTest : public ledger::TestWithEnvironment {
 public:
  // Wrapper around LevelDbFactory. This class is needed because LevelDbFactory
  // can only be deleted while the loop is running so that it can synchronized
  // with the io dispatcher.
  class LevelDbFactoryWrapper {
   public:
    LevelDbFactoryWrapper(async::TestLoop* loop, ledger::Environment* environment,
                          ledger::DetachedPath cache_path)
        : loop_(loop),
          environment_(environment),
          db_factory_(std::make_unique<LevelDbFactory>(environment, std::move(cache_path))) {}
    ~LevelDbFactoryWrapper() {
      async::PostTask(environment_->dispatcher(), [this] { db_factory_.reset(); });
      loop_->RunUntilIdle();
    }

    LevelDbFactory* operator->() const { return db_factory_.get(); };

   private:
    async::TestLoop* loop_;
    ledger::Environment* environment_;
    std::unique_ptr<LevelDbFactory> db_factory_;
  };

  LevelDbFactoryTest()
      : base_path_(tmpfs_.root_fd()),
        cache_path_(base_path_.SubPath("cache")),
        db_path_(base_path_.SubPath("databases")),
        db_factory_(&test_loop(), &environment_, cache_path_) {}

  LevelDbFactoryTest(const LevelDbFactoryTest&) = delete;
  LevelDbFactoryTest& operator=(const LevelDbFactoryTest&) = delete;
  ~LevelDbFactoryTest() override = default;

  // ledger::TestWithEnvironment:
  void SetUp() override {
    ledger::TestWithEnvironment::SetUp();

    ASSERT_TRUE(files::CreateDirectoryAt(cache_path_.root_fd(), cache_path_.path()));
    ASSERT_TRUE(files::CreateDirectoryAt(db_path_.root_fd(), db_path_.path()));

    db_factory_->Init();
    RunLoopUntilIdle();
  }

 private:
  scoped_tmpfs::ScopedTmpFS tmpfs_;
  ledger::DetachedPath base_path_;

 protected:
  ledger::DetachedPath cache_path_;
  ledger::DetachedPath db_path_;
  LevelDbFactoryWrapper db_factory_;
};

TEST_F(LevelDbFactoryTest, GetOrCreateDb) {
  // Create a new instance.
  Status status;
  std::unique_ptr<Db> db;
  bool called;
  db_factory_->GetOrCreateDb(db_path_.SubPath("db"), DbFactory::OnDbNotFound::CREATE,
                             callback::Capture(callback::SetWhenCalled(&called), &status, &db));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_NE(nullptr, db);
  // Write one key-value pair.
  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    std::unique_ptr<Db::Batch> batch;
    EXPECT_EQ(db->StartBatch(handler, &batch), Status::OK);
    EXPECT_EQ(batch->Put(handler, "key", "value"), Status::OK);
    EXPECT_EQ(batch->Execute(handler), Status::OK);
  });

  // Close the previous instance and open it again.
  db.reset();
  db_factory_->GetOrCreateDb(db_path_.SubPath("db"), DbFactory::OnDbNotFound::RETURN,
                             callback::Capture(callback::SetWhenCalled(&called), &status, &db));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_NE(nullptr, db);
  // Expect to find the previously written key-value pair.
  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    std::string value;
    EXPECT_EQ(db->Get(handler, "key", &value), Status::OK);
    EXPECT_EQ(value, "value");
  });
}

TEST_F(LevelDbFactoryTest, GetDbOnNotFound) {
  // Try to get a non existing Db and expect a |PAGE_NOT_FOUND| status.
  Status status;
  std::unique_ptr<Db> db;
  bool called;
  db_factory_->GetOrCreateDb(db_path_.SubPath("db"), DbFactory::OnDbNotFound::RETURN,
                             callback::Capture(callback::SetWhenCalled(&called), &status, &db));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::PAGE_NOT_FOUND);
  EXPECT_EQ(db, nullptr);
}

TEST_F(LevelDbFactoryTest, CreateMultipleDbs) {
  int N = 5;
  Status status;
  std::unique_ptr<Db> db;
  bool called;

  // Create N LevelDb instances, one after the other. All of them will use the
  // existing cached instance and then, initialize the creation of a new one.
  for (int i = 0; i < N; ++i) {
    ledger::DetachedPath path = db_path_.SubPath(absl::StrCat(i));
    EXPECT_FALSE(files::IsDirectoryAt(path.root_fd(), path.path()));

    db_factory_->GetOrCreateDb(path, DbFactory::OnDbNotFound::CREATE,
                               callback::Capture(callback::SetWhenCalled(&called), &status, &db));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(status, Status::OK);
    EXPECT_NE(nullptr, db);
    // Check that the directory was created.
    EXPECT_TRUE(files::IsDirectoryAt(path.root_fd(), path.path()));
  }
}

TEST_F(LevelDbFactoryTest, CreateMultipleDbsConcurrently) {
  int N = 5;
  Status statuses[N];
  std::unique_ptr<Db> dbs[N];
  bool called[N];

  // Create N LevelDb instances concurrently. The first one will use the cached
  // instance, the 2nd one will be queued up to get the cached one when it's
  // initialized, and all the others will be created directly at the destination
  // directory.
  for (int i = 0; i < N; ++i) {
    ledger::DetachedPath path = db_path_.SubPath(absl::StrCat(i));
    EXPECT_FALSE(files::IsDirectoryAt(path.root_fd(), path.path()));

    db_factory_->GetOrCreateDb(
        db_path_.SubPath(absl::StrCat(i)), DbFactory::OnDbNotFound::CREATE,
        callback::Capture(callback::SetWhenCalled(&called[i]), &statuses[i], &dbs[i]));
  }
  RunLoopUntilIdle();

  for (int i = 0; i < N; ++i) {
    ledger::DetachedPath path = db_path_.SubPath(absl::StrCat(i));
    EXPECT_TRUE(called[i]);
    EXPECT_EQ(statuses[i], Status::OK);
    EXPECT_NE(nullptr, dbs[i]);
    // Check that the directory was created.
    EXPECT_TRUE(files::IsDirectoryAt(path.root_fd(), path.path()));
  }
}

TEST_F(LevelDbFactoryTest, GetOrCreateDbInCallback) {
  bool called1;
  ledger::DetachedPath path1 = db_path_.SubPath("1");

  bool called2;
  ledger::DetachedPath path2 = db_path_.SubPath("2");
  Status status2;
  std::unique_ptr<Db> db2;

  db_factory_->GetOrCreateDb(
      path1, DbFactory::OnDbNotFound::CREATE, [&](Status status1, std::unique_ptr<Db> db1) {
        called1 = true;
        EXPECT_EQ(status1, Status::OK);
        EXPECT_NE(nullptr, db1);
        db_factory_->GetOrCreateDb(
            path2, DbFactory::OnDbNotFound::CREATE,
            callback::Capture(callback::SetWhenCalled(&called2), &status2, &db2));
      });
  RunLoopUntilIdle();
  EXPECT_TRUE(called1);
  EXPECT_TRUE(called2);
  EXPECT_EQ(status2, Status::OK);
  EXPECT_NE(nullptr, db2);

  // Check that the directories were created.
  EXPECT_TRUE(files::IsDirectoryAt(path1.root_fd(), path1.path()));
  EXPECT_TRUE(files::IsDirectoryAt(path2.root_fd(), path2.path()));
}

TEST_F(LevelDbFactoryTest, InitWithCachedDbAvailable) {
  // When an empty LevelDb instance is already cached from a previous
  // LevelDbFactory execution, don't create a new instance, but use the existing
  // one directly.
  scoped_tmpfs::ScopedTmpFS tmpfs;
  ledger::DetachedPath cache_path(tmpfs.root_fd(), "cache");
  // Must be the same as |kCachedDbPath| in leveldb_factory.cc.
  ledger::DetachedPath cached_db_path = cache_path.SubPath("cached_db");

  auto db_factory =
      std::make_unique<LevelDbFactoryWrapper>(&test_loop(), &environment_, cache_path);

  // The cached db directory should not be created, yet.
  EXPECT_FALSE(files::IsDirectoryAt(cached_db_path.root_fd(), cached_db_path.path()));

  // Initialize and wait for the cached instance to be created.
  (*db_factory)->Init();
  RunLoopUntilIdle();

  // Close the factory. This will not affect the created cached instance, which
  // was created under |cached_db_path|.
  db_factory.reset();
  EXPECT_TRUE(files::IsDirectoryAt(cached_db_path.root_fd(), cached_db_path.path()));

  // Re-initialize the factory object. It should now use the previously created
  // instance.
  db_factory = std::make_unique<LevelDbFactoryWrapper>(&test_loop(), &environment_, cache_path);
  (*db_factory)->Init();
  RunLoopUntilIdle();
}

// Make sure we can destroy the factory while a request is in progress.
TEST_F(LevelDbFactoryTest, QuitWhenBusy) {
  auto db_factory_ptr =
      std::make_unique<LevelDbFactoryWrapper>(&test_loop(), &environment_, cache_path_);
  (*db_factory_ptr)->Init();
  RunLoopUntilIdle();

  Status status;
  std::unique_ptr<Db> db;
  bool called;

  // Post the initialization code to the I/O loop.
  (*db_factory_ptr)
      ->GetOrCreateDb(db_path_.SubPath(absl::StrCat(0)), DbFactory::OnDbNotFound::CREATE,
                      callback::Capture(callback::SetWhenCalled(&called), &status, &db));

  // Delete the factory before any code is run on the I/O loop. The destructor
  // will block until all I/O operation are cancelled.
  db_factory_ptr.reset();

  // Pump all loops.
  RunLoopUntilIdle();

  // The behavior depends on what code is run on the I/O loop, vs main loop. If
  // the destruction happens first, no callback is ever called and |called| is
  // false. Otherwise, the callback can be called with either an OK status or a
  // ILLEGAL_STATE status, depending on how far the operation progressed on the
  // IO thread.
  if (called) {
    EXPECT_THAT(status, AnyOf(Status::OK, Status::ILLEGAL_STATE));
  }
}

}  // namespace
}  // namespace storage
