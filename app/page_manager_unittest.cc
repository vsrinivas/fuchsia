// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/app/page_manager.h"

#include <memory>

#include "apps/ledger/app/constants.h"
#include "apps/ledger/storage/fake/fake_page_storage.h"
#include "apps/ledger/storage/public/page_storage.h"
#include "apps/ledger/storage/public/types.h"
#include "gtest/gtest.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace ledger {
namespace {

class PageManagerTest : public ::testing::Test {
 public:
  PageManagerTest() {}
  ~PageManagerTest() override {}

 protected:
  // ApplicationTestBase:
  void SetUp() override {
    ::testing::Test::SetUp();
    page_id_ = storage::PageId(kPageIdSize, 'a');
  }

  mtl::MessageLoop message_loop_;
  storage::PageId page_id_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(PageManagerTest);
};

TEST_F(PageManagerTest, OnEmptyCallback) {
  bool on_empty_called = false;
  PageManager page_manager(
      std::make_unique<storage::fake::FakePageStorage>(page_id_),
      [this, &on_empty_called] {
        on_empty_called = true;
        message_loop_.QuitNow();
      });

  EXPECT_FALSE(on_empty_called);
  PagePtr page1 = page_manager.GetPagePtr();
  PagePtr page2 = page_manager.GetPagePtr();
  page1.reset();
  page2.reset();
  message_loop_.Run();
  EXPECT_TRUE(on_empty_called);

  on_empty_called = false;
  PagePtr page3 = page_manager.GetPagePtr();
  page3.reset();
  message_loop_.Run();
  EXPECT_TRUE(on_empty_called);
}

TEST_F(PageManagerTest, DeletingPageManagerClosesConnections) {
  std::unique_ptr<PageManager> page_manager = std::make_unique<PageManager>(
      std::make_unique<storage::fake::FakePageStorage>(page_id_), [] {});

  PagePtr page = page_manager->GetPagePtr();
  bool page_closed = false;
  page.set_connection_error_handler([this, &page_closed] {
    page_closed = true;
    message_loop_.QuitNow();
  });

  page_manager.reset();
  message_loop_.Run();
  EXPECT_TRUE(page_closed);
}

}  // namespace
}  // namespace ledger
