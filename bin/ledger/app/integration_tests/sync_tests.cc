// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/integration_tests/integration_test.h"
#include "apps/ledger/src/callback/capture.h"
#include "apps/ledger/src/convert/convert.h"
#include "lib/mtl/vmo/strings.h"

namespace ledger {
namespace integration_tests {
namespace {

class SyncIntegrationTest : public IntegrationTest {
 protected:
  ::testing::AssertionResult GetEntries(Page* page,
                                        fidl::Array<EntryPtr>* entries) {
    PageSnapshotPtr snapshot;
    Status status;
    page->GetSnapshot(snapshot.NewRequest(), nullptr, nullptr,
                      callback::Capture(MakeQuitTask(), &status));
    if (RunLoopWithTimeout() || status != Status::OK) {
      return ::testing::AssertionFailure() << "Unable to retrieve a snapshot";
    }
    entries->resize(0);
    fidl::Array<uint8_t> token = nullptr;
    fidl::Array<uint8_t> next_token = nullptr;
    do {
      fidl::Array<EntryPtr> new_entries;
      snapshot->GetEntries(nullptr, std::move(token),
                           callback::Capture(MakeQuitTask(), &status,
                                             &new_entries, &next_token));
      if (RunLoopWithTimeout() || status != Status::OK) {
        return ::testing::AssertionFailure() << "Unable to retrieve entries";
      }
      for (auto& entry : new_entries) {
        entries->push_back(std::move(entry));
      }
      token = std::move(next_token);
    } while (token);
    return ::testing::AssertionSuccess();
  }
};

TEST_F(SyncIntegrationTest, SerialConnection) {
  auto instance1 = NewLedgerAppInstance();
  auto page = instance1->GetTestPage();
  Status status;
  page->Put(convert::ToArray("Hello"), convert::ToArray("World"),
            callback::Capture(MakeQuitTask(), &status));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  fidl::Array<uint8_t> page_id;
  page->GetId(callback::Capture(MakeQuitTask(), &page_id));
  ASSERT_FALSE(RunLoopWithTimeout());

  auto instance2 = NewLedgerAppInstance();
  page = instance2->GetPage(page_id, Status::OK);
  EXPECT_TRUE(RunLoopUntil([this, &page] {
    fidl::Array<EntryPtr> entries;
    if (!GetEntries(page.get(), &entries)) {
      return true;
    }
    return !entries.empty();
  }));

  PageSnapshotPtr snapshot;
  page->GetSnapshot(snapshot.NewRequest(), nullptr, nullptr,
                    callback::Capture(MakeQuitTask(), &status));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  fidl::Array<uint8_t> value;
  snapshot->GetInline(convert::ToArray("Hello"),
                      callback::Capture(MakeQuitTask(), &status, &value));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  ASSERT_EQ("World", convert::ToString(value));
}

TEST_F(SyncIntegrationTest, ConcurrentConnection) {
  auto instance1 = NewLedgerAppInstance();
  auto instance2 = NewLedgerAppInstance();

  auto page1 = instance1->GetTestPage();
  fidl::Array<uint8_t> page_id;
  page1->GetId(callback::Capture(MakeQuitTask(), &page_id));
  ASSERT_FALSE(RunLoopWithTimeout());
  auto page2 = instance2->GetPage(page_id, Status::OK);

  Status status;
  page1->Put(convert::ToArray("Hello"), convert::ToArray("World"),
             callback::Capture(MakeQuitTask(), &status));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);

  EXPECT_TRUE(RunLoopUntil([this, &page2] {
    fidl::Array<EntryPtr> entries;
    if (!GetEntries(page2.get(), &entries)) {
      return true;
    }
    return !entries.empty();
  }));

  PageSnapshotPtr snapshot;
  page2->GetSnapshot(snapshot.NewRequest(), nullptr, nullptr,
                     callback::Capture(MakeQuitTask(), &status));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  fidl::Array<uint8_t> value;
  snapshot->GetInline(convert::ToArray("Hello"),
                      callback::Capture(MakeQuitTask(), &status, &value));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  ASSERT_EQ("World", convert::ToString(value));
}

}  // namespace
}  // namespace integration_tests
}  // namespace ledger
