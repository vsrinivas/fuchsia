// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_usage_db.h"

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <zircon/syscalls.h>

#include "gtest/gtest.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/storage/fake/fake_db.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/lib/fxl/macros.h"

namespace ledger {
namespace {

class PageUsageDbTest : public TestWithEnvironment {
 public:
  PageUsageDbTest()
      : db_(environment_.clock(), std::make_unique<storage::fake::FakeDb>(
                                      environment_.dispatcher())) {}

  ~PageUsageDbTest() override {}

  std::string RandomString(size_t size) {
    std::string result;
    result.resize(size);
    environment_.random()->Draw(&result);
    return result;
  }

 protected:
  PageUsageDb db_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageUsageDbTest);
};

TEST_F(PageUsageDbTest, GetPagesEmpty) {
  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    std::string ledger_name = "ledger_name";
    std::string page_id(::fuchsia::ledger::kPageIdSize, 'p');

    std::unique_ptr<storage::Iterator<const PageInfo>> pages;
    EXPECT_EQ(storage::Status::OK, db_.GetPages(handler, &pages));

    EXPECT_EQ(storage::Status::OK, pages->GetStatus());
    EXPECT_FALSE(pages->Valid());
  });
}

TEST_F(PageUsageDbTest, MarkPageOpened) {
  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    std::string ledger_name = "ledger_name";
    std::string page_id(::fuchsia::ledger::kPageIdSize, 'p');

    // Open the same page.
    EXPECT_EQ(storage::Status::OK,
              db_.MarkPageOpened(handler, ledger_name, page_id));

    // Expect to find a single entry with the opened page marker timestamp.
    std::unique_ptr<storage::Iterator<const PageInfo>> pages;
    EXPECT_EQ(storage::Status::OK, db_.GetPages(handler, &pages));

    EXPECT_EQ(storage::Status::OK, pages->GetStatus());
    EXPECT_TRUE(pages->Valid());
    EXPECT_EQ(ledger_name, (*pages)->ledger_name);
    EXPECT_EQ(page_id, (*pages)->page_id);
    EXPECT_EQ(PageInfo::kOpenedPageTimestamp, (*pages)->timestamp);

    pages->Next();
    EXPECT_EQ(storage::Status::OK, pages->GetStatus());
    EXPECT_FALSE(pages->Valid());
  });
}

TEST_F(PageUsageDbTest, MarkPageOpenedAndClosed) {
  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    std::string ledger_name = "ledger_name";
    std::string page_id(::fuchsia::ledger::kPageIdSize, 'p');

    // Open and close the same page.
    EXPECT_EQ(storage::Status::OK,
              db_.MarkPageOpened(handler, ledger_name, page_id));
    EXPECT_EQ(storage::Status::OK,
              db_.MarkPageClosed(handler, ledger_name, page_id));

    // Expect to find a single entry with timestamp != the opened page marker
    // timestamp.
    std::unique_ptr<storage::Iterator<const PageInfo>> pages;
    EXPECT_EQ(storage::Status::OK, db_.GetPages(handler, &pages));

    EXPECT_EQ(storage::Status::OK, pages->GetStatus());
    EXPECT_TRUE(pages->Valid());
    EXPECT_EQ(ledger_name, (*pages)->ledger_name);
    EXPECT_EQ(page_id, (*pages)->page_id);
    EXPECT_NE(PageInfo::kOpenedPageTimestamp, (*pages)->timestamp);

    pages->Next();
    EXPECT_EQ(storage::Status::OK, pages->GetStatus());
    EXPECT_FALSE(pages->Valid());
  });
}

TEST_F(PageUsageDbTest, MarkAllPagesClosed) {
  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    std::string ledger_name = "ledger_name";
    int N = 5;
    std::string page_ids[N];
    for (int i = 0; i < N; ++i) {
      page_ids[i] = RandomString(::fuchsia::ledger::kPageIdSize);
    }

    // Open 5 pages.
    for (int i = 0; i < N; ++i) {
      EXPECT_EQ(storage::Status::OK,
                db_.MarkPageOpened(handler, ledger_name, page_ids[i]));
    }

    // Close 1 of them.
    EXPECT_EQ(storage::Status::OK,
              db_.MarkPageClosed(handler, ledger_name, page_ids[0]));

    // Expect to find 4 entries with timestamp equal to the opened page marker
    // timestamp.
    std::unique_ptr<storage::Iterator<const PageInfo>> pages;
    EXPECT_EQ(storage::Status::OK, db_.GetPages(handler, &pages));

    int open_pages_count = 0;
    zx::time_utc page_0_timestamp(0);
    for (int i = 0; i < N; ++i) {
      EXPECT_EQ(storage::Status::OK, pages->GetStatus());
      ASSERT_TRUE(pages->Valid());
      EXPECT_EQ(ledger_name, (*pages)->ledger_name);
      if ((*pages)->page_id == page_ids[0]) {
        page_0_timestamp = (*pages)->timestamp;
        EXPECT_NE(PageInfo::kOpenedPageTimestamp, page_0_timestamp);
      } else {
        ++open_pages_count;
        EXPECT_EQ(PageInfo::kOpenedPageTimestamp, (*pages)->timestamp);
      }
      pages->Next();
    }
    EXPECT_EQ(N - 1, open_pages_count);

    EXPECT_EQ(storage::Status::OK, pages->GetStatus());
    EXPECT_FALSE(pages->Valid());

    // Call MarkAllPagesClosed and expect all 5 pages to be closed, 4 with the
    // same value.
    EXPECT_EQ(storage::Status::OK, db_.MarkAllPagesClosed(handler));

    EXPECT_EQ(storage::Status::OK, db_.GetPages(handler, &pages));
    zx::time_utc timestamp(PageInfo::kOpenedPageTimestamp);
    for (int i = 0; i < N; ++i) {
      EXPECT_EQ(storage::Status::OK, pages->GetStatus());
      ASSERT_TRUE(pages->Valid());
      EXPECT_EQ(ledger_name, (*pages)->ledger_name);
      if ((*pages)->page_id == page_ids[0]) {
        EXPECT_EQ(page_0_timestamp, (*pages)->timestamp);
      } else {
        // Expect from page 0, the others should have the same timestamp.
        EXPECT_NE(PageInfo::kOpenedPageTimestamp, (*pages)->timestamp);
        if (timestamp == PageInfo::kOpenedPageTimestamp) {
          timestamp = (*pages)->timestamp;
        } else {
          EXPECT_EQ(timestamp, (*pages)->timestamp);
        }
      }
      pages->Next();
    }
    EXPECT_EQ(N - 1, open_pages_count);
  });
}

}  // namespace
}  // namespace ledger
