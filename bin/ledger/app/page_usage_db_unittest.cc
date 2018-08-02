// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/ledger_repository_impl.h"

#include <zircon/syscalls.h>

#include "gtest/gtest.h"
#include "lib/callback/capture.h"
#include "lib/callback/set_when_called.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"
#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/app/ledger_repository_factory_impl.h"
#include "peridot/bin/ledger/storage/public/types.h"
#include "peridot/bin/ledger/testing/test_with_environment.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"

namespace ledger {
namespace {

std::string RandomString(size_t size) {
  std::string result;
  result.resize(size);
  zx_cprng_draw(&result[0], size);
  return result;
}

class PageUsageDbTest : public TestWithEnvironment {
 public:
  PageUsageDbTest() : db_(dispatcher(), DetachedPath(tmpfs_.root_fd())) {}

  ~PageUsageDbTest() override {}

 protected:
  scoped_tmpfs::ScopedTmpFS tmpfs_;
  PageUsageDb db_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageUsageDbTest);
};

TEST_F(PageUsageDbTest, Init) { EXPECT_EQ(Status::OK, db_.Init()); }

TEST_F(PageUsageDbTest, GetPagesEmpty) {
  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    std::string ledger_name = "ledger_name";
    std::string page_id(kPageIdSize, 'p');

    EXPECT_EQ(Status::OK, db_.Init());
    std::unique_ptr<storage::Iterator<const PageUsageDb::PageInfo>> pages;
    EXPECT_EQ(Status::OK, db_.GetPages(handler, &pages));

    EXPECT_EQ(storage::Status::OK, pages->GetStatus());
    EXPECT_FALSE(pages->Valid());
  });
}

TEST_F(PageUsageDbTest, MarkPageOpened) {
  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    std::string ledger_name = "ledger_name";
    std::string page_id(kPageIdSize, 'p');

    EXPECT_EQ(Status::OK, db_.Init());
    // Open the same page.
    EXPECT_EQ(Status::OK, db_.MarkPageOpened(handler, ledger_name, page_id));

    // Expect to find a single entry with 0 timestamp.
    std::unique_ptr<storage::Iterator<const PageUsageDb::PageInfo>> pages;
    EXPECT_EQ(Status::OK, db_.GetPages(handler, &pages));

    EXPECT_EQ(storage::Status::OK, pages->GetStatus());
    EXPECT_TRUE(pages->Valid());
    EXPECT_EQ(ledger_name, (*pages)->ledger_name);
    EXPECT_EQ(page_id, (*pages)->page_id);
    EXPECT_EQ(0u, (*pages)->timestamp.get());

    pages->Next();
    EXPECT_EQ(storage::Status::OK, pages->GetStatus());
    EXPECT_FALSE(pages->Valid());
  });
}

TEST_F(PageUsageDbTest, MarkPageOpenedAndClosed) {
  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    std::string ledger_name = "ledger_name";
    std::string page_id(kPageIdSize, 'p');

    EXPECT_EQ(Status::OK, db_.Init());
    // Open and close the same page.
    EXPECT_EQ(Status::OK, db_.MarkPageOpened(handler, ledger_name, page_id));
    EXPECT_EQ(Status::OK, db_.MarkPageClosed(handler, ledger_name, page_id));

    // Expect to find a single entry with timestamp > 0.
    std::unique_ptr<storage::Iterator<const PageUsageDb::PageInfo>> pages;
    EXPECT_EQ(Status::OK, db_.GetPages(handler, &pages));

    EXPECT_EQ(storage::Status::OK, pages->GetStatus());
    EXPECT_TRUE(pages->Valid());
    EXPECT_EQ(ledger_name, (*pages)->ledger_name);
    EXPECT_EQ(page_id, (*pages)->page_id);
    EXPECT_LT(0u, (*pages)->timestamp.get());

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
      page_ids[i] = RandomString(kPageIdSize);
    }

    EXPECT_EQ(Status::OK, db_.Init());
    // Open 5 pages.
    for (int i = 0; i < N; ++i) {
      EXPECT_EQ(Status::OK,
                db_.MarkPageOpened(handler, ledger_name, page_ids[i]));
    }

    // Close 1 of them.
    EXPECT_EQ(Status::OK,
              db_.MarkPageClosed(handler, ledger_name, page_ids[0]));

    // Expect to find a 4 entries with timestamp equal to 0.
    std::unique_ptr<storage::Iterator<const PageUsageDb::PageInfo>> pages;
    EXPECT_EQ(Status::OK, db_.GetPages(handler, &pages));

    int open_pages_count = 0;
    zx::time page_0_timestamp(0);
    for (int i = 0; i < N; ++i) {
      EXPECT_EQ(storage::Status::OK, pages->GetStatus());
      ASSERT_TRUE(pages->Valid());
      EXPECT_EQ(ledger_name, (*pages)->ledger_name);
      if ((*pages)->page_id == page_ids[0]) {
        page_0_timestamp = (*pages)->timestamp;
        EXPECT_LT(0u, page_0_timestamp.get());
      } else {
        ++open_pages_count;
        EXPECT_EQ(0u, (*pages)->timestamp.get());
      }
      pages->Next();
    }
    EXPECT_EQ(N - 1, open_pages_count);

    EXPECT_EQ(storage::Status::OK, pages->GetStatus());
    EXPECT_FALSE(pages->Valid());

    // Call MarkAllPagesClosed and expect all 5 pages to be closed, 4 with the
    // same value.
    EXPECT_EQ(Status::OK, db_.MarkAllPagesClosed(handler));

    EXPECT_EQ(Status::OK, db_.GetPages(handler, &pages));
    zx::time timestamp(0);
    for (int i = 0; i < N; ++i) {
      EXPECT_EQ(storage::Status::OK, pages->GetStatus());
      ASSERT_TRUE(pages->Valid());
      EXPECT_EQ(ledger_name, (*pages)->ledger_name);
      if ((*pages)->page_id == page_ids[0]) {
        EXPECT_EQ(page_0_timestamp, (*pages)->timestamp);
      } else {
        // Expect from page 0, the others should have the same timestamp.
        EXPECT_LT(0u, (*pages)->timestamp.get());
        if (timestamp.get() == 0) {
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
