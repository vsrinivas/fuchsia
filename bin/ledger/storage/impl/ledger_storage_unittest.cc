// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/ledger_storage_impl.h"

#include <memory>

#include "apps/ledger/src/callback/capture.h"
#include "apps/ledger/src/coroutine/coroutine_impl.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/macros.h"
#include "lib/fsl/tasks/message_loop.h"

namespace storage {
namespace {

class LedgerStorageTest : public test::TestWithMessageLoop {
 public:
  LedgerStorageTest()
      : storage_(&coroutine_service_, tmp_dir_.path(), "test_app") {}

  ~LedgerStorageTest() override {}

 private:
  files::ScopedTempDir tmp_dir_;
  coroutine::CoroutineServiceImpl coroutine_service_;

 protected:
  LedgerStorageImpl storage_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerStorageTest);
};

TEST_F(LedgerStorageTest, CreateGetCreatePageStorage) {
  PageId page_id = "1234";
  Status status;
  std::unique_ptr<PageStorage> page_storage;

  storage_.GetPageStorage(
      page_id, callback::Capture(MakeQuitTask(), &status, &page_storage));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::NOT_FOUND, status);
  EXPECT_EQ(nullptr, page_storage);

  storage_.CreatePageStorage(
      page_id, callback::Capture(MakeQuitTask(), &status, &page_storage));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  ASSERT_NE(nullptr, page_storage);
  ASSERT_EQ(page_id, page_storage->GetId());

  page_storage.reset();
  storage_.GetPageStorage(
      page_id, callback::Capture(MakeQuitTask(), &status, &page_storage));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  EXPECT_NE(nullptr, page_storage);
}

TEST_F(LedgerStorageTest, CreateDeletePageStorage) {
  PageId page_id = "1234";
  Status status;
  std::unique_ptr<PageStorage> page_storage;
  storage_.CreatePageStorage(
      page_id, callback::Capture(MakeQuitTask(), &status, &page_storage));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  ASSERT_NE(nullptr, page_storage);
  ASSERT_EQ(page_id, page_storage->GetId());
  page_storage.reset();

  storage_.GetPageStorage(
      page_id, callback::Capture(MakeQuitTask(), &status, &page_storage));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  EXPECT_NE(nullptr, page_storage);

  EXPECT_TRUE(storage_.DeletePageStorage(page_id));
  storage_.GetPageStorage(
      page_id, callback::Capture(MakeQuitTask(), &status, &page_storage));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::NOT_FOUND, status);
  EXPECT_EQ(nullptr, page_storage);
}

}  // namespace
}  // namespace storage
