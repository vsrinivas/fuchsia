// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/callback/waiter.h>

#include "peridot/bin/ledger/testing/ledger_matcher.h"
#include "peridot/bin/ledger/tests/integration/integration_test.h"
#include "peridot/bin/ledger/tests/integration/test_utils.h"

using testing::IsEmpty;

namespace ledger {
namespace {

// Tests in this suite execute a series of operations and check the content of
// the Page afterwards.
class PageMutationTest : public test::integration::IntegrationTest {
 public:
  void SetUp() override {
    app_instance_ = NewLedgerAppInstance();
    page_ = app_instance_->GetTestPage();
  }

  ledger::PageSnapshotPtr PageGetSnapshot() {
    ledger::Status status;
    ledger::PageSnapshotPtr snapshot;
    auto waiter = NewWaiter();
    page_->GetSnapshot(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                       nullptr,
                       callback::Capture(waiter->GetCallback(), &status));
    waiter->RunUntilCalled();
    EXPECT_EQ(ledger::Status::OK, status);
    return snapshot;
  }

  std::vector<ledger::Entry> GetEntries() {
    ledger::PageSnapshotPtr snapshot = PageGetSnapshot();
    return SnapshotGetEntries(this, &snapshot);
  }

  testing::AssertionResult Put(std::string key, std::string value) {
    return Do("Put", [&](auto callback) {
      page_->Put(convert::ToArray(key), convert::ToArray(value),
                 std::move(callback));
    });
  }

  testing::AssertionResult Delete(std::string key) {
    return Do("Delete", [&](auto callback) {
      page_->Delete(convert::ToArray(key), std::move(callback));
    });
  }

  testing::AssertionResult Clear() {
    return Do("Clear",
              [&](auto callback) { page_->Clear(std::move(callback)); });
  }

  testing::AssertionResult StartTransaction() {
    return Do("StartTransaction", [&](auto callback) {
      page_->StartTransaction(std::move(callback));
    });
  }

  testing::AssertionResult Commit() {
    return Do("Commit",
              [&](auto callback) { page_->Commit(std::move(callback)); });
  }

  testing::AssertionResult Rollback() {
    return Do("Rollback",
              [&](auto callback) { page_->Rollback(std::move(callback)); });
  }

 private:
  // Executes the given action on the current page.
  //
  // This helper function handles the heavy lifting of calling an operation of
  // the page, waiting for the result and returning an assertion error in case
  // of non ok status. It expects |action| to operate the operation on the page
  // and returns the status in its callback.
  testing::AssertionResult Do(
      std::string operation_name,
      fit::function<void(fit::function<void(Status)>)> action) {
    Status status;
    auto waiter = NewWaiter();
    action(callback::Capture(waiter->GetCallback(), &status));
    waiter->RunUntilCalled();
    if (status == Status::OK) {
      return testing::AssertionSuccess();
    }
    return testing::AssertionFailure()
           << "Error while executing " << operation_name
           << ". Status: " << static_cast<uint32_t>(status);
  }

 protected:
  std::unique_ptr<test::LedgerAppInstanceFactory::LedgerAppInstance>
      app_instance_;
  ledger::PagePtr page_;
};

TEST_P(PageMutationTest, InitialSnapshotIsEmpty) {
  EXPECT_THAT(GetEntries(), IsEmpty());
}

TEST_P(PageMutationTest, PutOutsideOfTransaction) {
  ASSERT_TRUE(Put("key", "value"));

  ASSERT_THAT(GetEntries(), EntriesMatch({{"key", "value"}}));

  ASSERT_TRUE(Put("key2", "value2"));

  ASSERT_THAT(GetEntries(),
              EntriesMatch({{"key", "value"}, {"key2", "value2"}}));
}

TEST_P(PageMutationTest, PutInsideOfTransaction) {
  ASSERT_TRUE(StartTransaction());
  ASSERT_TRUE(Put("key", "value"));

  ASSERT_THAT(GetEntries(), IsEmpty());

  ASSERT_TRUE(Put("key2", "value2"));
  ASSERT_TRUE(Commit());

  ASSERT_THAT(GetEntries(),
              EntriesMatch({{"key", "value"}, {"key2", "value2"}}));
}

TEST_P(PageMutationTest, RollbackTransaction) {
  ASSERT_TRUE(StartTransaction());
  ASSERT_TRUE(Put("key", "value"));

  ASSERT_THAT(GetEntries(), IsEmpty());

  ASSERT_TRUE(Put("key2", "value2"));
  ASSERT_TRUE(Rollback());

  ASSERT_THAT(GetEntries(), IsEmpty());
}

TEST_P(PageMutationTest, DeleteOutsideOfTransaction) {
  ASSERT_TRUE(Put("key", "value"));
  ASSERT_TRUE(Put("key2", "value2"));
  ASSERT_THAT(GetEntries(),
              EntriesMatch({{"key", "value"}, {"key2", "value2"}}));

  ASSERT_TRUE(Delete("key"));

  ASSERT_THAT(GetEntries(), EntriesMatch({{"key2", "value2"}}));
}

TEST_P(PageMutationTest, DeleteInsideOfTransaction) {
  ASSERT_TRUE(Put("key", "value"));
  ASSERT_TRUE(Put("key2", "value2"));
  ASSERT_THAT(GetEntries(),
              EntriesMatch({{"key", "value"}, {"key2", "value2"}}));

  ASSERT_TRUE(StartTransaction());
  ASSERT_TRUE(Delete("key"));
  ASSERT_TRUE(Put("key3", "value3"));
  ASSERT_TRUE(Delete("key3"));
  ASSERT_TRUE(Commit());

  ASSERT_THAT(GetEntries(), EntriesMatch({{"key2", "value2"}}));
}

TEST_P(PageMutationTest, ClearOutsideOfTransaction) {
  ASSERT_TRUE(Put("key", "value"));
  ASSERT_TRUE(Put("key2", "value2"));
  ASSERT_THAT(GetEntries(),
              EntriesMatch({{"key", "value"}, {"key2", "value2"}}));

  ASSERT_TRUE(Clear());

  ASSERT_THAT(GetEntries(), IsEmpty());
}

TEST_P(PageMutationTest, ClearInsideOfTransaction) {
  ASSERT_TRUE(Put("key", "value"));
  ASSERT_TRUE(Put("key2", "value2"));
  ASSERT_THAT(GetEntries(),
              EntriesMatch({{"key", "value"}, {"key2", "value2"}}));

  ASSERT_TRUE(StartTransaction());
  ASSERT_TRUE(Put("key3", "value3"));
  ASSERT_TRUE(Clear());
  ASSERT_TRUE(Put("key4", "value4"));
  ASSERT_TRUE(Commit());

  ASSERT_THAT(GetEntries(), EntriesMatch({{"key4", "value4"}}));
}

TEST_P(PageMutationTest, MultipleClearCallsInsideOfTransaction) {
  ASSERT_TRUE(Put("key", "value"));
  ASSERT_TRUE(Put("key2", "value2"));
  ASSERT_THAT(GetEntries(),
              EntriesMatch({{"key", "value"}, {"key2", "value2"}}));

  ASSERT_TRUE(StartTransaction());
  ASSERT_TRUE(Put("key3", "value3"));
  ASSERT_TRUE(Clear());
  ASSERT_TRUE(Put("key4", "value4"));
  ASSERT_TRUE(Clear());
  ASSERT_TRUE(Put("key5", "value5"));
  ASSERT_TRUE(Commit());

  ASSERT_THAT(GetEntries(), EntriesMatch({{"key5", "value5"}}));
}

TEST_P(PageMutationTest, ClearAndDeleteInsideOfTransaction) {
  ASSERT_TRUE(Put("key", "value"));
  ASSERT_THAT(GetEntries(), EntriesMatch({{"key", "value"}}));

  ASSERT_TRUE(StartTransaction());
  ASSERT_TRUE(Clear());
  ASSERT_TRUE(Delete("key"));
  ASSERT_TRUE(Commit());

  ASSERT_THAT(GetEntries(), IsEmpty());
}

TEST_P(PageMutationTest, DeleteAndClearInsideOfTransaction) {
  ASSERT_TRUE(Put("key", "value"));
  ASSERT_THAT(GetEntries(), EntriesMatch({{"key", "value"}}));

  ASSERT_TRUE(StartTransaction());
  ASSERT_TRUE(Delete("key"));
  ASSERT_TRUE(Clear());
  ASSERT_TRUE(Commit());

  ASSERT_THAT(GetEntries(), IsEmpty());
}

TEST_P(PageMutationTest, ClearAndRestoreInsideTransaction) {
  ASSERT_TRUE(Put("key", "value"));
  ASSERT_THAT(GetEntries(), EntriesMatch({{"key", "value"}}));

  ASSERT_TRUE(StartTransaction());
  ASSERT_TRUE(Clear());
  ASSERT_TRUE(Put("key", "value"));
  ASSERT_TRUE(Commit());

  ASSERT_THAT(GetEntries(), EntriesMatch({{"key", "value"}}));
}

INSTANTIATE_TEST_CASE_P(
    PageMutationTest, PageMutationTest,
    ::testing::ValuesIn(test::GetLedgerAppInstanceFactories()));

}  // namespace
}  // namespace ledger
