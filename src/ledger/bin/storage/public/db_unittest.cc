// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/public/db_unittest.h"

namespace storage {
namespace {

TEST_P(DbTest, PutGet) {
  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    std::unique_ptr<Db::Batch> batch;
    EXPECT_EQ(db_->StartBatch(handler, &batch), Status::OK);
    EXPECT_EQ(batch->Put(handler, "key", "value"), Status::OK);
    EXPECT_EQ(batch->Execute(handler), Status::OK);

    std::string value;
    EXPECT_EQ(db_->Get(handler, "key", &value), Status::OK);
    EXPECT_EQ(value, "value");
  });
}

TEST_P(DbTest, HasKey) {
  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    std::unique_ptr<Db::Batch> batch;
    EXPECT_EQ(db_->StartBatch(handler, &batch), Status::OK);
    EXPECT_EQ(batch->Put(handler, "key", "value"), Status::OK);
    EXPECT_EQ(batch->Execute(handler), Status::OK);

    EXPECT_EQ(db_->HasKey(handler, "key"), Status::OK);
    EXPECT_EQ(db_->HasKey(handler, "key2"), Status::INTERNAL_NOT_FOUND);
  });
}

TEST_P(DbTest, HasPrefix) {
  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    std::unique_ptr<Db::Batch> batch;
    EXPECT_EQ(db_->StartBatch(handler, &batch), Status::OK);
    EXPECT_EQ(batch->Put(handler, "key", "value"), Status::OK);
    EXPECT_EQ(batch->Execute(handler), Status::OK);

    EXPECT_EQ(db_->HasPrefix(handler, ""), Status::OK);
    EXPECT_EQ(db_->HasPrefix(handler, "k"), Status::OK);
    EXPECT_EQ(db_->HasPrefix(handler, "ke"), Status::OK);
    EXPECT_EQ(db_->HasPrefix(handler, "key"), Status::OK);
    EXPECT_EQ(db_->HasPrefix(handler, "key2"), Status::INTERNAL_NOT_FOUND);
  });
}

}  // namespace
}  // namespace storage
