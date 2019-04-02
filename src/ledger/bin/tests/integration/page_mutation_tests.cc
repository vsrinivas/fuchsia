// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/callback/waiter.h>

#include "src/ledger/bin/testing/ledger_matcher.h"
#include "src/ledger/bin/tests/integration/integration_test.h"
#include "src/ledger/bin/tests/integration/test_utils.h"

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
    page_->GetSnapshotNew(snapshot.NewRequest(),
                          fidl::VectorPtr<uint8_t>::New(0), nullptr);
    return SnapshotGetEntries(this, &snapshot);
  }

  void Put(std::string key, std::string value) {
    page_->PutNew(convert::ToArray(key), convert::ToArray(value));
  }

  void Delete(std::string key) { page_->DeleteNew(convert::ToArray(key)); }

 protected:
  std::unique_ptr<LedgerAppInstanceFactory::LedgerAppInstance> app_instance_;
  PagePtr page_;
};

TEST_P(PageMutationTest, InitialSnapshotIsEmpty) {
  EXPECT_THAT(GetEntries(), IsEmpty());
}

TEST_P(PageMutationTest, PutOutsideOfTransaction) {
  Put("key", "value");

  ASSERT_THAT(GetEntries(), MatchEntries({{"key", "value"}}));

  Put("key2", "value2");

  ASSERT_THAT(GetEntries(),
              MatchEntries({{"key", "value"}, {"key2", "value2"}}));
}

TEST_P(PageMutationTest, PutInsideOfTransaction) {
  page_->StartTransactionNew();
  Put("key", "value");

  ASSERT_THAT(GetEntries(), IsEmpty());

  Put("key2", "value2");
  page_->CommitNew();

  ASSERT_THAT(GetEntries(),
              MatchEntries({{"key", "value"}, {"key2", "value2"}}));
}

TEST_P(PageMutationTest, RollbackTransaction) {
  page_->StartTransactionNew();
  Put("key", "value");

  ASSERT_THAT(GetEntries(), IsEmpty());

  Put("key2", "value2");
  page_->RollbackNew();

  ASSERT_THAT(GetEntries(), IsEmpty());
}

TEST_P(PageMutationTest, DeleteOutsideOfTransaction) {
  Put("key", "value");
  Put("key2", "value2");
  ASSERT_THAT(GetEntries(),
              MatchEntries({{"key", "value"}, {"key2", "value2"}}));

  Delete("key");

  ASSERT_THAT(GetEntries(), MatchEntries({{"key2", "value2"}}));
}

TEST_P(PageMutationTest, DeleteInsideOfTransaction) {
  Put("key", "value");
  Put("key2", "value2");
  ASSERT_THAT(GetEntries(),
              MatchEntries({{"key", "value"}, {"key2", "value2"}}));

  page_->StartTransactionNew();
  Delete("key");
  Put("key3", "value3");
  Delete("key3");
  page_->CommitNew();

  ASSERT_THAT(GetEntries(), MatchEntries({{"key2", "value2"}}));
}

TEST_P(PageMutationTest, ClearOutsideOfTransaction) {
  Put("key", "value");
  Put("key2", "value2");
  ASSERT_THAT(GetEntries(),
              MatchEntries({{"key", "value"}, {"key2", "value2"}}));

  page_->ClearNew();

  ASSERT_THAT(GetEntries(), IsEmpty());
}

TEST_P(PageMutationTest, ClearInsideOfTransaction) {
  Put("key", "value");
  Put("key2", "value2");
  ASSERT_THAT(GetEntries(),
              MatchEntries({{"key", "value"}, {"key2", "value2"}}));

  page_->StartTransactionNew();
  Put("key3", "value3");
  page_->ClearNew();
  Put("key4", "value4");
  page_->CommitNew();

  ASSERT_THAT(GetEntries(), MatchEntries({{"key4", "value4"}}));
}

TEST_P(PageMutationTest, MultipleClearCallsInsideOfTransaction) {
  Put("key", "value");
  Put("key2", "value2");
  ASSERT_THAT(GetEntries(),
              MatchEntries({{"key", "value"}, {"key2", "value2"}}));

  page_->StartTransactionNew();
  Put("key3", "value3");
  page_->ClearNew();
  Put("key4", "value4");
  page_->ClearNew();
  Put("key5", "value5");
  page_->CommitNew();

  ASSERT_THAT(GetEntries(), MatchEntries({{"key5", "value5"}}));
}

TEST_P(PageMutationTest, ClearAndDeleteInsideOfTransaction) {
  Put("key", "value");
  ASSERT_THAT(GetEntries(), MatchEntries({{"key", "value"}}));

  page_->StartTransactionNew();
  page_->ClearNew();
  Delete("key");
  page_->CommitNew();

  ASSERT_THAT(GetEntries(), IsEmpty());
}

TEST_P(PageMutationTest, DeleteAndClearInsideOfTransaction) {
  Put("key", "value");
  ASSERT_THAT(GetEntries(), MatchEntries({{"key", "value"}}));

  page_->StartTransactionNew();
  Delete("key");
  page_->ClearNew();
  page_->CommitNew();

  ASSERT_THAT(GetEntries(), IsEmpty());
}

TEST_P(PageMutationTest, ClearAndRestoreInsideTransaction) {
  Put("key", "value");
  ASSERT_THAT(GetEntries(), MatchEntries({{"key", "value"}}));

  page_->StartTransactionNew();
  page_->ClearNew();
  Put("key", "value");
  page_->CommitNew();

  ASSERT_THAT(GetEntries(), MatchEntries({{"key", "value"}}));
}

INSTANTIATE_TEST_SUITE_P(
    PageMutationTest, PageMutationTest,
    ::testing::ValuesIn(GetLedgerAppInstanceFactoryBuilders()));

}  // namespace
}  // namespace ledger
