// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ledger/internal/cpp/fidl.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ledger/bin/testing/ledger_matcher.h"
#include "src/ledger/bin/tests/integration/integration_test.h"
#include "src/ledger/bin/tests/integration/test_utils.h"
#include "src/lib/callback/waiter.h"

using testing::IsEmpty;

namespace ledger {
namespace {

// Tests in this suite execute a series of operations and check the content of
// the Page afterwards.
class PageMutationTest : public IntegrationTest {
 public:
  void SetUp() override {
    IntegrationTest::SetUp();
    app_instance_ = NewLedgerAppInstance();
    page_ = app_instance_->GetTestPage();
  }

  std::vector<Entry> GetEntries() {
    PageSnapshotPtr snapshot;
    page_->GetSnapshot(snapshot.NewRequest(), std::vector<uint8_t>(), nullptr);
    return SnapshotGetEntries(this, &snapshot);
  }

  void Put(std::string key, std::string value) {
    page_->Put(convert::ToArray(key), convert::ToArray(value));
  }

  void Delete(std::string key) { page_->Delete(convert::ToArray(key)); }

 protected:
  std::unique_ptr<LedgerAppInstanceFactory::LedgerAppInstance> app_instance_;
  PagePtr page_;
};

TEST_P(PageMutationTest, InitialSnapshotIsEmpty) { EXPECT_THAT(GetEntries(), IsEmpty()); }

TEST_P(PageMutationTest, PutOutsideOfTransaction) {
  Put("key", "value");

  ASSERT_THAT(GetEntries(), MatchEntries({{"key", "value"}}));

  Put("key2", "value2");

  ASSERT_THAT(GetEntries(), MatchEntries({{"key", "value"}, {"key2", "value2"}}));
}

TEST_P(PageMutationTest, PutInsideOfTransaction) {
  page_->StartTransaction();
  Put("key", "value");

  ASSERT_THAT(GetEntries(), IsEmpty());

  Put("key2", "value2");
  page_->Commit();

  ASSERT_THAT(GetEntries(), MatchEntries({{"key", "value"}, {"key2", "value2"}}));
}

TEST_P(PageMutationTest, RollbackTransaction) {
  page_->StartTransaction();
  Put("key", "value");

  ASSERT_THAT(GetEntries(), IsEmpty());

  Put("key2", "value2");
  page_->Rollback();

  ASSERT_THAT(GetEntries(), IsEmpty());
}

TEST_P(PageMutationTest, DeleteOutsideOfTransaction) {
  Put("key", "value");
  Put("key2", "value2");
  ASSERT_THAT(GetEntries(), MatchEntries({{"key", "value"}, {"key2", "value2"}}));

  Delete("key");

  ASSERT_THAT(GetEntries(), MatchEntries({{"key2", "value2"}}));
}

TEST_P(PageMutationTest, DeleteInsideOfTransaction) {
  Put("key", "value");
  Put("key2", "value2");
  ASSERT_THAT(GetEntries(), MatchEntries({{"key", "value"}, {"key2", "value2"}}));

  page_->StartTransaction();
  Delete("key");
  Put("key3", "value3");
  Delete("key3");
  page_->Commit();

  ASSERT_THAT(GetEntries(), MatchEntries({{"key2", "value2"}}));
}

TEST_P(PageMutationTest, ClearOutsideOfTransaction) {
  Put("key", "value");
  Put("key2", "value2");
  ASSERT_THAT(GetEntries(), MatchEntries({{"key", "value"}, {"key2", "value2"}}));

  page_->Clear();

  ASSERT_THAT(GetEntries(), IsEmpty());
}

TEST_P(PageMutationTest, ClearInsideOfTransaction) {
  Put("key", "value");
  Put("key2", "value2");
  ASSERT_THAT(GetEntries(), MatchEntries({{"key", "value"}, {"key2", "value2"}}));

  page_->StartTransaction();
  Put("key3", "value3");
  page_->Clear();
  Put("key4", "value4");
  page_->Commit();

  ASSERT_THAT(GetEntries(), MatchEntries({{"key4", "value4"}}));
}

TEST_P(PageMutationTest, MultipleClearCallsInsideOfTransaction) {
  Put("key", "value");
  Put("key2", "value2");
  ASSERT_THAT(GetEntries(), MatchEntries({{"key", "value"}, {"key2", "value2"}}));

  page_->StartTransaction();
  Put("key3", "value3");
  page_->Clear();
  Put("key4", "value4");
  page_->Clear();
  Put("key5", "value5");
  page_->Commit();

  ASSERT_THAT(GetEntries(), MatchEntries({{"key5", "value5"}}));
}

TEST_P(PageMutationTest, ClearAndDeleteInsideOfTransaction) {
  Put("key", "value");
  ASSERT_THAT(GetEntries(), MatchEntries({{"key", "value"}}));

  page_->StartTransaction();
  page_->Clear();
  Delete("key");
  page_->Commit();

  ASSERT_THAT(GetEntries(), IsEmpty());
}

TEST_P(PageMutationTest, DeleteAndClearInsideOfTransaction) {
  Put("key", "value");
  ASSERT_THAT(GetEntries(), MatchEntries({{"key", "value"}}));

  page_->StartTransaction();
  Delete("key");
  page_->Clear();
  page_->Commit();

  ASSERT_THAT(GetEntries(), IsEmpty());
}

TEST_P(PageMutationTest, ClearAndRestoreInsideTransaction) {
  Put("key", "value");
  ASSERT_THAT(GetEntries(), MatchEntries({{"key", "value"}}));

  page_->StartTransaction();
  page_->Clear();
  Put("key", "value");
  page_->Commit();

  ASSERT_THAT(GetEntries(), MatchEntries({{"key", "value"}}));
}

INSTANTIATE_TEST_SUITE_P(PageMutationTest, PageMutationTest,
                         ::testing::ValuesIn(GetLedgerAppInstanceFactoryBuilders()),
                         PrintLedgerAppInstanceFactoryBuilder());

}  // namespace
}  // namespace ledger
