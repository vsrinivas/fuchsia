// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_usage_db.h"

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <zircon/syscalls.h>

#include <memory>

#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/db_view_factory.h"
#include "src/ledger/bin/app/serialization.h"
#include "src/ledger/bin/storage/fake/fake_db.h"
#include "src/ledger/bin/storage/fake/fake_db_factory.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"

namespace ledger {
namespace {

class PageUsageDbTest : public TestWithEnvironment {
 public:
  PageUsageDbTest() : db_factory_(dispatcher()) {}
  PageUsageDbTest(const PageUsageDbTest&) = delete;
  PageUsageDbTest& operator=(const PageUsageDbTest&) = delete;
  ~PageUsageDbTest() override = default;

  std::string RandomString(size_t size) {
    std::string result;
    result.resize(size);
    environment_.random()->Draw(&result);
    return result;
  }

  void SetUp() override {
    Status status;
    RunInCoroutine([this, &status](coroutine::CoroutineHandler* handler) {
      std::unique_ptr<storage::Db> leveldb;
      if (coroutine::SyncCall(
              handler,
              [this](fit::function<void(Status, std::unique_ptr<storage::Db>)> callback) mutable {
                db_factory_.GetOrCreateDb(DetachedPath(tmpfs_.root_fd()),
                                          storage::DbFactory::OnDbNotFound::CREATE,
                                          std::move(callback));
              },
              &status, &leveldb) == coroutine::ContinuationStatus::INTERRUPTED) {
        status = Status::INTERRUPTED;
        return;
      }
      dbview_factory_ = std::make_unique<DbViewFactory>(std::move(leveldb));
    });
    db_ = std::make_unique<PageUsageDb>(
        &environment_, dbview_factory_->CreateDbView(RepositoryRowPrefix::PAGE_USAGE_DB));
    FXL_DCHECK(status == Status::OK);
  }

 protected:
  scoped_tmpfs::ScopedTmpFS tmpfs_;
  storage::fake::FakeDbFactory db_factory_;
  std::unique_ptr<DbViewFactory> dbview_factory_;
  std::unique_ptr<PageUsageDb> db_;
};

TEST_F(PageUsageDbTest, GetPagesEmpty) {
  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    std::string ledger_name = "ledger_name";
    std::string page_id(::fuchsia::ledger::PAGE_ID_SIZE, 'p');

    std::unique_ptr<storage::Iterator<const PageInfo>> pages;
    EXPECT_EQ(db_->GetPages(handler, &pages), Status::OK);

    EXPECT_EQ(pages->GetStatus(), Status::OK);
    EXPECT_FALSE(pages->Valid());
  });
}

TEST_F(PageUsageDbTest, MarkPageOpened) {
  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    std::string ledger_name = "ledger_name";
    std::string page_id(::fuchsia::ledger::PAGE_ID_SIZE, 'p');

    // Open the same page.
    EXPECT_EQ(db_->MarkPageOpened(handler, ledger_name, page_id), Status::OK);

    // Expect to find a single entry with the opened page marker timestamp.
    std::unique_ptr<storage::Iterator<const PageInfo>> pages;
    EXPECT_EQ(db_->GetPages(handler, &pages), Status::OK);

    EXPECT_EQ(pages->GetStatus(), Status::OK);
    EXPECT_TRUE(pages->Valid());
    EXPECT_EQ((*pages)->ledger_name, ledger_name);
    EXPECT_EQ((*pages)->page_id, page_id);
    EXPECT_EQ((*pages)->timestamp, PageInfo::kOpenedPageTimestamp);

    pages->Next();
    EXPECT_EQ(pages->GetStatus(), Status::OK);
    EXPECT_FALSE(pages->Valid());
  });
}

TEST_F(PageUsageDbTest, MarkPageOpenedAndClosed) {
  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    std::string ledger_name = "ledger_name";
    std::string page_id(::fuchsia::ledger::PAGE_ID_SIZE, 'p');

    // Open and close the same page.
    EXPECT_EQ(db_->MarkPageOpened(handler, ledger_name, page_id), Status::OK);
    EXPECT_EQ(db_->MarkPageClosed(handler, ledger_name, page_id), Status::OK);

    // Expect to find a single entry with timestamp != the opened page marker
    // timestamp.
    std::unique_ptr<storage::Iterator<const PageInfo>> pages;
    EXPECT_EQ(db_->GetPages(handler, &pages), Status::OK);

    EXPECT_EQ(pages->GetStatus(), Status::OK);
    EXPECT_TRUE(pages->Valid());
    EXPECT_EQ((*pages)->ledger_name, ledger_name);
    EXPECT_EQ((*pages)->page_id, page_id);
    EXPECT_NE(PageInfo::kOpenedPageTimestamp, (*pages)->timestamp);

    pages->Next();
    EXPECT_EQ(pages->GetStatus(), Status::OK);
    EXPECT_FALSE(pages->Valid());
  });
}

TEST_F(PageUsageDbTest, MarkAllPagesClosed) {
  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    std::string ledger_name = "ledger_name";
    int N = 5;
    std::string page_ids[N];
    for (int i = 0; i < N; ++i) {
      page_ids[i] = RandomString(::fuchsia::ledger::PAGE_ID_SIZE);
    }

    // Open 5 pages.
    for (int i = 0; i < N; ++i) {
      EXPECT_EQ(db_->MarkPageOpened(handler, ledger_name, page_ids[i]), Status::OK);
    }

    // Close 1 of them.
    EXPECT_EQ(db_->MarkPageClosed(handler, ledger_name, page_ids[0]), Status::OK);

    // Expect to find 4 entries with timestamp equal to the opened page marker
    // timestamp.
    std::unique_ptr<storage::Iterator<const PageInfo>> pages;
    EXPECT_EQ(db_->GetPages(handler, &pages), Status::OK);

    int open_pages_count = 0;
    zx::time_utc page_0_timestamp(0);
    for (int i = 0; i < N; ++i) {
      EXPECT_EQ(pages->GetStatus(), Status::OK);
      ASSERT_TRUE(pages->Valid());
      EXPECT_EQ((*pages)->ledger_name, ledger_name);
      if ((*pages)->page_id == page_ids[0]) {
        page_0_timestamp = (*pages)->timestamp;
        EXPECT_NE(PageInfo::kOpenedPageTimestamp, page_0_timestamp);
      } else {
        ++open_pages_count;
        EXPECT_EQ((*pages)->timestamp, PageInfo::kOpenedPageTimestamp);
      }
      pages->Next();
    }
    EXPECT_EQ(open_pages_count, N - 1);

    EXPECT_EQ(pages->GetStatus(), Status::OK);
    EXPECT_FALSE(pages->Valid());

    // Call MarkAllPagesClosed and expect all 5 pages to be closed, 4 with the
    // same value.
    EXPECT_EQ(db_->MarkAllPagesClosed(handler), Status::OK);

    EXPECT_EQ(db_->GetPages(handler, &pages), Status::OK);
    zx::time_utc timestamp(PageInfo::kOpenedPageTimestamp);
    for (int i = 0; i < N; ++i) {
      EXPECT_EQ(pages->GetStatus(), Status::OK);
      ASSERT_TRUE(pages->Valid());
      EXPECT_EQ((*pages)->ledger_name, ledger_name);
      if ((*pages)->page_id == page_ids[0]) {
        EXPECT_EQ((*pages)->timestamp, page_0_timestamp);
      } else {
        // Expect from page 0, the others should have the same timestamp.
        EXPECT_NE(PageInfo::kOpenedPageTimestamp, (*pages)->timestamp);
        if (timestamp == PageInfo::kOpenedPageTimestamp) {
          timestamp = (*pages)->timestamp;
        } else {
          EXPECT_EQ((*pages)->timestamp, timestamp);
        }
      }
      pages->Next();
    }
    EXPECT_EQ(open_pages_count, N - 1);
  });
}

// Verifies that calls to the Db are correctly queued until an initialization of PageUsageDb is
// completed.
TEST_F(PageUsageDbTest, OperationsQueueWhileInitRunning) {
  std::string ledger_name = "ledger_name";
  std::string page_id(::fuchsia::ledger::PAGE_ID_SIZE, 'p');
  std::unique_ptr<storage::Iterator<const PageInfo>> pages;

  // Open a page and reset the database before closing it.
  EXPECT_TRUE(RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    EXPECT_EQ(db_->MarkPageOpened(handler, ledger_name, page_id), Status::OK);
    EXPECT_EQ(db_->GetPages(handler, &pages), Status::OK);
    // Some pages are open.
    EXPECT_TRUE(pages->Valid());
    EXPECT_EQ((*pages)->ledger_name, ledger_name);
    EXPECT_EQ((*pages)->page_id, page_id);
    EXPECT_EQ((*pages)->timestamp, PageInfo::kOpenedPageTimestamp);
  }));

  db_ = std::make_unique<PageUsageDb>(
      &environment_, dbview_factory_->CreateDbView(RepositoryRowPrefix::PAGE_USAGE_DB));

  // All pages should be closed, because |GetPages| triggered internal initialization.
  EXPECT_TRUE(RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    EXPECT_EQ(db_->GetPages(handler, &pages), Status::OK);
    // All pages are closed, because initialization finished.
    EXPECT_EQ((*pages)->ledger_name, ledger_name);
    EXPECT_EQ((*pages)->page_id, page_id);
    EXPECT_NE((*pages)->timestamp, PageInfo::kOpenedPageTimestamp);
  }));
}

}  // namespace
}  // namespace ledger
