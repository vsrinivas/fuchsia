// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/db_view_factory.h"

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/app/serialization.h"
#include "src/ledger/bin/storage/fake/fake_db.h"
#include "src/ledger/bin/storage/public/db_unittest.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/run_in_coroutine.h"
#include "src/ledger/bin/testing/test_with_environment.h"

namespace ledger {
namespace {

using ::testing::ElementsAre;

using DbViewTest = TestWithEnvironment;

TEST_F(DbViewTest, Serialization) {
  // If this test fails, it means the serialization format changed and kSerializationVersion must
  // be updated.
  auto base_db = std::make_unique<storage::fake::FakeDb>(environment_.dispatcher());
  auto base_db_ptr = base_db.get();
  auto dbview_factory = std::make_unique<DbViewFactory>(std::move(base_db));
  auto dbview_1 = dbview_factory->CreateDbView(RepositoryRowPrefix::PAGE_USAGE_DB);
  auto dbview_2 = dbview_factory->CreateDbView(RepositoryRowPrefix::CLOCKS);

  EXPECT_TRUE(RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    std::unique_ptr<storage::Db::Batch> batch;
    EXPECT_EQ(dbview_1->StartBatch(handler, &batch), Status::OK);
    EXPECT_EQ(batch->Put(handler, "key", "value1"), Status::OK);
    EXPECT_EQ(batch->Execute(handler), Status::OK);

    EXPECT_EQ(dbview_2->StartBatch(handler, &batch), Status::OK);
    EXPECT_EQ(batch->Put(handler, "key", "value2"), Status::OK);
    EXPECT_EQ(batch->Execute(handler), Status::OK);

    std::string value_1;
    EXPECT_EQ(base_db_ptr->Get(handler, " key", &value_1), Status::OK);
    EXPECT_EQ(value_1, "value1");

    std::string value_2;
    EXPECT_EQ(base_db_ptr->Get(handler, "!key", &value_2), Status::OK);
    EXPECT_EQ(value_2, "value2");
  }));
}

TEST_F(DbViewTest, TwoDbsPutGet) {
  auto base_db = std::make_unique<storage::fake::FakeDb>(environment_.dispatcher());
  auto dbview_factory = std::make_unique<DbViewFactory>(std::move(base_db));
  auto dbview_1 = dbview_factory->CreateDbView(RepositoryRowPrefix::PAGE_USAGE_DB);
  auto dbview_2 = dbview_factory->CreateDbView(RepositoryRowPrefix::CLOCKS);

  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    std::unique_ptr<storage::Db::Batch> batch;
    EXPECT_EQ(dbview_1->StartBatch(handler, &batch), Status::OK);
    EXPECT_EQ(batch->Put(handler, "key", "value1"), Status::OK);
    EXPECT_EQ(batch->Execute(handler), Status::OK);

    EXPECT_EQ(dbview_2->StartBatch(handler, &batch), Status::OK);
    EXPECT_EQ(batch->Put(handler, "key", "value2"), Status::OK);
    EXPECT_EQ(batch->Execute(handler), Status::OK);

    std::string value_1;
    EXPECT_EQ(dbview_1->Get(handler, "key", &value_1), Status::OK);
    EXPECT_EQ(value_1, "value1");

    std::string value_2;
    EXPECT_EQ(dbview_2->Get(handler, "key", &value_2), Status::OK);
    EXPECT_EQ(value_2, "value2");
  });
}

TEST_F(DbViewTest, TwoDbsGetByPrefix) {
  auto base_db = std::make_unique<storage::fake::FakeDb>(environment_.dispatcher());
  auto dbview_factory = std::make_unique<DbViewFactory>(std::move(base_db));
  auto dbview_1 = dbview_factory->CreateDbView(RepositoryRowPrefix::PAGE_USAGE_DB);
  auto dbview_2 = dbview_factory->CreateDbView(RepositoryRowPrefix::CLOCKS);

  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    std::unique_ptr<storage::Db::Batch> batch;
    EXPECT_EQ(dbview_1->StartBatch(handler, &batch), Status::OK);
    EXPECT_EQ(batch->Put(handler, "key1.1", "value1.1"), Status::OK);
    EXPECT_EQ(batch->Put(handler, "key1.2", "value1.2"), Status::OK);
    EXPECT_EQ(batch->Execute(handler), Status::OK);

    EXPECT_EQ(dbview_2->StartBatch(handler, &batch), Status::OK);
    EXPECT_EQ(batch->Put(handler, "key2.1", "value2.1"), Status::OK);
    EXPECT_EQ(batch->Put(handler, "key2.2", "value2.2"), Status::OK);
    EXPECT_EQ(batch->Execute(handler), Status::OK);

    std::vector<std::string> keys_1;
    EXPECT_EQ(dbview_1->GetByPrefix(handler, "key", &keys_1), Status::OK);
    EXPECT_THAT(keys_1, ElementsAre("1.1", "1.2"));

    std::vector<std::string> keys_2;
    EXPECT_EQ(dbview_2->GetByPrefix(handler, "key", &keys_2), Status::OK);
    EXPECT_THAT(keys_2, ElementsAre("2.1", "2.2"));
  });
}

TEST_F(DbViewTest, TwoDbsGetIteratorByPrefix) {
  auto base_db = std::make_unique<storage::fake::FakeDb>(environment_.dispatcher());
  auto dbview_factory = std::make_unique<DbViewFactory>(std::move(base_db));
  auto dbview_1 = dbview_factory->CreateDbView(RepositoryRowPrefix::PAGE_USAGE_DB);
  auto dbview_2 = dbview_factory->CreateDbView(RepositoryRowPrefix::CLOCKS);

  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    std::unique_ptr<storage::Db::Batch> batch;
    EXPECT_EQ(dbview_1->StartBatch(handler, &batch), Status::OK);
    EXPECT_EQ(batch->Put(handler, "key1.1", "value1.1"), Status::OK);
    EXPECT_EQ(batch->Put(handler, "key1.2", "value1.2"), Status::OK);
    EXPECT_EQ(batch->Execute(handler), Status::OK);

    EXPECT_EQ(dbview_2->StartBatch(handler, &batch), Status::OK);
    EXPECT_EQ(batch->Put(handler, "key2.1", "value2.1"), Status::OK);
    EXPECT_EQ(batch->Put(handler, "key2.2", "value2.2"), Status::OK);
    EXPECT_EQ(batch->Execute(handler), Status::OK);

    std::unique_ptr<storage::Iterator<
        const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>>>
        iterator_1;
    EXPECT_EQ(dbview_1->GetIteratorAtPrefix(handler, "key", &iterator_1), Status::OK);
    ASSERT_TRUE(iterator_1->Valid());
    EXPECT_EQ((**iterator_1).first, "key1.1");
    EXPECT_EQ((**iterator_1).second, "value1.1");
    iterator_1->Next();
    ASSERT_TRUE(iterator_1->Valid());
    EXPECT_EQ((*iterator_1)->first, "key1.2");
    EXPECT_EQ((*iterator_1)->second, "value1.2");
    iterator_1->Next();
    EXPECT_FALSE(iterator_1->Valid());
  });
}

}  // namespace
}  // namespace ledger

// Parametrized tests for DbView.
namespace storage {
namespace {
class DbViewTestFactory : public storage::DbTestFactory {
 public:
  DbViewTestFactory() = default;

  std::unique_ptr<storage::Db> GetDb(ledger::Environment* environment,
                                     ledger::ScopedTmpLocation* /*tmp_location*/) override {
    if (!dbview_factory_) {
      auto base_db = std::make_unique<storage::fake::FakeDb>(environment->dispatcher());
      dbview_factory_ = std::make_unique<ledger::DbViewFactory>(std::move(base_db));
    }
    return dbview_factory_->CreateDbView(ledger::RepositoryRowPrefix::PAGE_USAGE_DB);
  }

 private:
  std::unique_ptr<ledger::DbViewFactory> dbview_factory_;
};

INSTANTIATE_TEST_SUITE_P(DbViewTest, DbTest,
                         testing::Values([] { return std::make_unique<DbViewTestFactory>(); }));
}  // namespace
}  // namespace storage
