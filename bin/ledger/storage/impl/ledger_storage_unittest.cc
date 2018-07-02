// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "gtest/gtest.h"
#include "lib/callback/capture.h"
#include "lib/callback/set_when_called.h"
#include "lib/fxl/macros.h"
#include "lib/gtest/test_loop_fixture.h"
#include "peridot/bin/ledger/coroutine/coroutine_impl.h"
#include "peridot/bin/ledger/encryption/fake/fake_encryption_service.h"
#include "peridot/bin/ledger/storage/impl/ledger_storage_impl.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"

namespace storage {
namespace {

class LedgerStorageTest : public gtest::TestLoopFixture {
 public:
  LedgerStorageTest()
      : encryption_service_(dispatcher()),
        storage_(dispatcher(), &coroutine_service_, &encryption_service_,
                 ledger::DetachedPath(tmpfs_.root_fd()), "test_app") {}

  ~LedgerStorageTest() override {}

 private:
  scoped_tmpfs::ScopedTmpFS tmpfs_;
  coroutine::CoroutineServiceImpl coroutine_service_;
  encryption::FakeEncryptionService encryption_service_;

 protected:
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
  EXPECT_EQ(Status::NOT_FOUND, status);
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

  EXPECT_TRUE(storage_.DeletePageStorage(page_id));
  storage_.GetPageStorage(
      page_id, callback::Capture(callback::SetWhenCalled(&called), &status,
                                 &page_storage));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::NOT_FOUND, status);
  EXPECT_EQ(nullptr, page_storage);
}

}  // namespace
}  // namespace storage
