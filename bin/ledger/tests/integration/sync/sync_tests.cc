// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/callback/capture.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/vmo/strings.h"
#include "peridot/bin/ledger/tests/integration/integration_test.h"
#include "peridot/lib/convert/convert.h"

namespace test {
namespace integration {
namespace sync {
namespace {

class SyncIntegrationTest : public IntegrationTest {
 protected:
  ::testing::AssertionResult GetEntries(
      ledger::Page* page, fidl::VectorPtr<ledger::Entry>* entries) {
    ledger::PageSnapshotPtr snapshot;
    ledger::Status status;
    page->GetSnapshot(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                      nullptr, callback::Capture(MakeQuitTask(), &status));
    RunLoop();
    if (status != ledger::Status::OK) {
      return ::testing::AssertionFailure() << "Unable to retrieve a snapshot";
    }
    entries->resize(0);
    std::unique_ptr<ledger::Token> token = nullptr;
    std::unique_ptr<ledger::Token> next_token = nullptr;
    do {
      fidl::VectorPtr<ledger::Entry> new_entries;
      snapshot->GetEntries(fidl::VectorPtr<uint8_t>::New(0), std::move(token),
                           callback::Capture(MakeQuitTask(), &status,
                                             &new_entries, &next_token));
      RunLoop();
      if (status != ledger::Status::OK) {
        return ::testing::AssertionFailure() << "Unable to retrieve entries";
      }
      for (size_t i = 0; i < new_entries->size(); ++i) {
        entries->push_back(std::move(new_entries->at(i)));
      }
      token = std::move(next_token);
    } while (token);
    return ::testing::AssertionSuccess();
  }
};

TEST_P(SyncIntegrationTest, SerialConnection) {
  auto instance1 = NewLedgerAppInstance();
  auto page = instance1->GetTestPage();
  ledger::Status status;
  page->Put(convert::ToArray("Hello"), convert::ToArray("World"),
            callback::Capture(MakeQuitTask(), &status));
  RunLoop();
  ASSERT_EQ(ledger::Status::OK, status);
  ledger::PageId page_id;
  page->GetId(callback::Capture(MakeQuitTask(), &page_id));
  RunLoop();

  auto instance2 = NewLedgerAppInstance();
  page = instance2->GetPage(fidl::MakeOptional(page_id), ledger::Status::OK);
  EXPECT_TRUE(RunLoopUntil([this, &page] {
    fidl::VectorPtr<ledger::Entry> entries;
    if (!GetEntries(page.get(), &entries)) {
      return true;
    }
    return !entries->empty();
  }));

  ledger::PageSnapshotPtr snapshot;
  page->GetSnapshot(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                    nullptr, callback::Capture(MakeQuitTask(), &status));
  RunLoop();
  ASSERT_EQ(ledger::Status::OK, status);
  std::unique_ptr<ledger::InlinedValue> inlined_value;
  snapshot->GetInline(
      convert::ToArray("Hello"),
      callback::Capture(MakeQuitTask(), &status, &inlined_value));
  RunLoop();
  ASSERT_EQ(ledger::Status::OK, status);
  ASSERT_TRUE(inlined_value);
  ASSERT_EQ("World", convert::ToString(inlined_value->value));
}

TEST_P(SyncIntegrationTest, ConcurrentConnection) {
  auto instance1 = NewLedgerAppInstance();
  auto instance2 = NewLedgerAppInstance();

  auto page1 = instance1->GetTestPage();
  ledger::PageId page_id;
  page1->GetId(callback::Capture(MakeQuitTask(), &page_id));
  RunLoop();
  auto page2 =
      instance2->GetPage(fidl::MakeOptional(page_id), ledger::Status::OK);

  ledger::Status status;
  page1->Put(convert::ToArray("Hello"), convert::ToArray("World"),
             callback::Capture(MakeQuitTask(), &status));
  RunLoop();
  ASSERT_EQ(ledger::Status::OK, status);

  EXPECT_TRUE(RunLoopUntil([this, &page2] {
    fidl::VectorPtr<ledger::Entry> entries;
    if (!GetEntries(page2.get(), &entries)) {
      return true;
    }
    return !entries->empty();
  }));

  ledger::PageSnapshotPtr snapshot;
  page2->GetSnapshot(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                     nullptr, callback::Capture(MakeQuitTask(), &status));
  RunLoop();
  ASSERT_EQ(ledger::Status::OK, status);
  std::unique_ptr<ledger::InlinedValue> inlined_value;
  snapshot->GetInline(
      convert::ToArray("Hello"),
      callback::Capture(MakeQuitTask(), &status, &inlined_value));
  RunLoop();
  ASSERT_EQ(ledger::Status::OK, status);
  ASSERT_TRUE(inlined_value);
  ASSERT_EQ("World", convert::ToString(inlined_value->value));
}

INSTANTIATE_TEST_CASE_P(SyncIntegrationTest, SyncIntegrationTest,
                        ::testing::ValuesIn(GetLedgerAppInstanceFactories()));

}  // namespace
}  // namespace sync
}  // namespace integration
}  // namespace test
