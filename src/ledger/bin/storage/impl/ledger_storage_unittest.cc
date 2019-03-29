// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <src/lib/fxl/macros.h>
#include <lib/gtest/test_loop_fixture.h>

#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/storage/fake/fake_db_factory.h"
#include "src/ledger/bin/storage/impl/ledger_storage_impl.h"
#include "src/ledger/bin/storage/public/db_factory.h"
#include "src/ledger/bin/testing/test_with_environment.h"

namespace storage {
namespace {

class LedgerStorageTest : public ledger::TestWithEnvironment {
 public:
  LedgerStorageTest()
      : encryption_service_(dispatcher()),
        db_factory_(dispatcher()),
        storage_(&environment_, &encryption_service_, &db_factory_,
                 ledger::DetachedPath(tmpfs_.root_fd())) {}

  ~LedgerStorageTest() override {}

  // ledger::TestWithEnvironment:
  void SetUp() override {
    ledger::TestWithEnvironment::SetUp();

    ASSERT_EQ(Status::OK, storage_.Init());
  }

 private:
  scoped_tmpfs::ScopedTmpFS tmpfs_;
  encryption::FakeEncryptionService encryption_service_;

 protected:
  fake::FakeDbFactory db_factory_;
  LedgerStorageImpl storage_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerStorageTest);
};

TEST_F(LedgerStorageTest, CreateGetCreatePageStorage) {
  PageId page_id = "1234";
  Status status;
  std::unique_ptr<PageStorage> page_storage;
  bool called;
  storage_.GetPageStorage(
      page_id, callback::Capture(callback::SetWhenCalled(&called), &status,
                                 &page_storage));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::PAGE_NOT_FOUND, status);
  EXPECT_EQ(nullptr, page_storage);

  storage_.CreatePageStorage(
      page_id, callback::Capture(callback::SetWhenCalled(&called), &status,
                                 &page_storage));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  ASSERT_EQ(Status::OK, status);
  ASSERT_NE(nullptr, page_storage);
  ASSERT_EQ(page_id, page_storage->GetId());

  page_storage.reset();
  storage_.GetPageStorage(
      page_id, callback::Capture(callback::SetWhenCalled(&called), &status,
                                 &page_storage));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_NE(nullptr, page_storage);
}

TEST_F(LedgerStorageTest, CreateDeletePageStorage) {
  PageId page_id = "1234";
  Status status;
  bool called;
  std::unique_ptr<PageStorage> page_storage;
  storage_.CreatePageStorage(
      page_id, callback::Capture(callback::SetWhenCalled(&called), &status,
                                 &page_storage));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  ASSERT_EQ(Status::OK, status);
  ASSERT_NE(nullptr, page_storage);
  ASSERT_EQ(page_id, page_storage->GetId());
  page_storage.reset();

  storage_.GetPageStorage(
      page_id, callback::Capture(callback::SetWhenCalled(&called), &status,
                                 &page_storage));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_NE(nullptr, page_storage);

  storage_.DeletePageStorage(
      page_id, callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  storage_.GetPageStorage(
      page_id, callback::Capture(callback::SetWhenCalled(&called), &status,
                                 &page_storage));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::PAGE_NOT_FOUND, status);
  EXPECT_EQ(nullptr, page_storage);
}

TEST_F(LedgerStorageTest, DeletePageStorageNotFound) {
  PageId page_id = "1234";
  Status status;
  bool called;

  storage_.DeletePageStorage(
      page_id, callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::PAGE_NOT_FOUND, status);
}

}  // namespace
}  // namespace storage
