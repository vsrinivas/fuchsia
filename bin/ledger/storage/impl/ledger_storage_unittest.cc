// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/ledger_storage_impl.h"

#include <memory>

#include "gtest/gtest.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace storage {
namespace {

class LedgerStorageTest : public ::testing::Test {
 public:
  LedgerStorageTest()
      : storage_(message_loop_.task_runner(), tmp_dir_.path(), "test_app") {}

  ~LedgerStorageTest() override {}

 private:
  files::ScopedTempDir tmp_dir_;

 protected:
  mtl::MessageLoop message_loop_;
  LedgerStorageImpl storage_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LedgerStorageTest);
};

TEST_F(LedgerStorageTest, CreateGetCreatePageStorage) {
  PageId page_id = "1234";
  storage_.GetPageStorage(
      page_id,
      [this](Status status, std::unique_ptr<PageStorage> page_storage) {
        EXPECT_EQ(Status::NOT_FOUND, status);
        EXPECT_EQ(nullptr, page_storage);
        message_loop_.PostQuitTask();
      });
  message_loop_.Run();

  std::unique_ptr<PageStorage> page_storage;
  ASSERT_EQ(Status::OK, storage_.CreatePageStorage(page_id, &page_storage));
  ASSERT_EQ(page_id, page_storage->GetId());
  page_storage.reset();
  storage_.GetPageStorage(
      page_id,
      [this](Status status, std::unique_ptr<PageStorage> page_storage) {
        EXPECT_EQ(Status::OK, status);
        EXPECT_NE(nullptr, page_storage);
        message_loop_.PostQuitTask();
      });
  message_loop_.Run();
}

TEST_F(LedgerStorageTest, CreateDeletePageStorage) {
  PageId page_id = "1234";
  std::unique_ptr<PageStorage> page_storage;
  ASSERT_EQ(Status::OK, storage_.CreatePageStorage(page_id, &page_storage));
  ASSERT_EQ(page_id, page_storage->GetId());
  page_storage.reset();
  storage_.GetPageStorage(
      page_id,
      [this](Status status, std::unique_ptr<PageStorage> page_storage) {
        EXPECT_EQ(Status::OK, status);
        EXPECT_NE(nullptr, page_storage);
        message_loop_.PostQuitTask();
      });
  message_loop_.Run();

  EXPECT_TRUE(storage_.DeletePageStorage(page_id));
  storage_.GetPageStorage(
      page_id,
      [this](Status status, std::unique_ptr<PageStorage> page_storage) {
        EXPECT_EQ(Status::NOT_FOUND, status);
        EXPECT_EQ(nullptr, page_storage);
        message_loop_.PostQuitTask();
      });
  message_loop_.Run();
}

}  // namespace
}  // namespace storage
