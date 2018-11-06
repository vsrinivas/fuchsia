// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/fxl/files/directory.h>
#include <lib/fxl/files/path.h>
#include <lib/fxl/macros.h>

#include "gtest/gtest.h"
#include "peridot/bin/ledger/filesystem/detached_path.h"
#include "peridot/bin/ledger/storage/impl/leveldb_factory.h"
#include "peridot/bin/ledger/testing/test_with_environment.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"

namespace storage {
namespace {

class LevelDbFactoryTest : public ledger::TestWithEnvironment {
 public:
  LevelDbFactoryTest()
      : base_path_(tmpfs_.root_fd()),
        cache_path_(base_path_.SubPath("cache")),
        db_path_(base_path_.SubPath("databases")),
        db_factory_(&environment_, cache_path_) {}

  ~LevelDbFactoryTest() override {}

  // ledger::TestWithEnvironment:
  void SetUp() override {
    ledger::TestWithEnvironment::SetUp();

    ASSERT_TRUE(
        files::CreateDirectoryAt(cache_path_.root_fd(), cache_path_.path()));
    ASSERT_TRUE(files::CreateDirectoryAt(db_path_.root_fd(), db_path_.path()));
  }

 private:
  scoped_tmpfs::ScopedTmpFS tmpfs_;
  ledger::DetachedPath base_path_;

 protected:
  ledger::DetachedPath cache_path_;
  ledger::DetachedPath db_path_;
  LevelDbFactory db_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LevelDbFactoryTest);
};

TEST_F(LevelDbFactoryTest, GetOrCreateDb) {
  // Create a new instance.
  Status status;
  std::unique_ptr<Db> db;
  bool called;
  db_factory_.GetOrCreateDb(
      db_path_.SubPath("db"),
      callback::Capture(callback::SetWhenCalled(&called), &status, &db));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_NE(nullptr, db);
  // Write one key-value pair.
  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    std::unique_ptr<Db::Batch> batch;
    EXPECT_EQ(Status::OK, db->StartBatch(handler, &batch));
    EXPECT_EQ(Status::OK, batch->Put(handler, "key", "value"));
    EXPECT_EQ(Status::OK, batch->Execute(handler));
  });

  // Close the previous instance and open it again.
  db.reset();
  db_factory_.GetOrCreateDb(
      db_path_.SubPath("db"),
      callback::Capture(callback::SetWhenCalled(&called), &status, &db));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_NE(nullptr, db);
  // Expect to find the previously written key-value pair.
  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    std::string value;
    EXPECT_EQ(Status::OK, db->Get(handler, "key", &value));
    EXPECT_EQ("value", value);
  });
}

}  // namespace
}  // namespace storage
