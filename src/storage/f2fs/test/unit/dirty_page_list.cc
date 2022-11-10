// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <safemath/checked_math.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/storage/f2fs/f2fs.h"
#include "unit_lib.h"

namespace f2fs {
namespace {

using DirtyPageListTest = F2fsFakeDevTestFixture;

TEST_F(DirtyPageListTest, AddAndRemoveDirtyPage) {
  fbl::RefPtr<fs::Vnode> test_file;
  root_dir_->Create("test", S_IFREG, &test_file);
  fbl::RefPtr<f2fs::File> vn = fbl::RefPtr<f2fs::File>::Downcast(std::move(test_file));

  ASSERT_EQ(fs_->GetDirtyDataPageList().Size(), 0U);
  {
    LockedPage locked_page;
    vn->GrabCachePage(0, &locked_page);

    // Add dirty Page
    locked_page.SetDirty();
    ASSERT_FALSE(locked_page->IsLastReference());
    ASSERT_EQ(locked_page->IsDirty(), true);
    ASSERT_EQ(locked_page->InTreeContainer(), true);
    ASSERT_EQ(locked_page->InListContainer(), true);
    ASSERT_EQ(fs_->GetDirtyDataPageList().Size(), 1U);
    // Duplicate add is ignored
    ASSERT_EQ(fs_->GetDirtyDataPageList().AddDirty(locked_page).status_value(),
              ZX_ERR_ALREADY_EXISTS);

    // Remove dirty Page
    ASSERT_TRUE(fs_->GetDirtyDataPageList().RemoveDirty(locked_page).is_ok());
    ASSERT_TRUE(locked_page->IsLastReference());
    locked_page->ClearDirtyForIo();
  }
  ASSERT_EQ(fs_->GetDirtyDataPageList().Size(), 0U);

  vn->Close();
  vn = nullptr;
}

TEST_F(DirtyPageListTest, TakeeDirtyPages) {
  fbl::RefPtr<fs::Vnode> test_file;
  root_dir_->Create("test", S_IFREG, &test_file);
  fbl::RefPtr<f2fs::File> vn = fbl::RefPtr<f2fs::File>::Downcast(std::move(test_file));
  char buf[kPageSize];

  // Make dirty Pages
  FileTester::AppendToFile(vn.get(), buf, kPageSize);
  FileTester::AppendToFile(vn.get(), buf, kPageSize);

  ASSERT_EQ(fs_->GetDirtyDataPageList().Size(), 2U);

  for (int i = 0; i < 2; ++i) {
    LockedPage locked_page;
    vn->GrabCachePage(i, &locked_page);
    ASSERT_EQ(locked_page->IsDirty(), true);
    ASSERT_EQ(locked_page->InTreeContainer(), true);
    ASSERT_EQ(locked_page->InListContainer(), true);
  }

  // Hold the lock of the 1st Page.
  LockedPage locked_page;
  vn->GrabCachePage(0, &locked_page);

  // Try to take 2 Pages from the list
  {
    auto pages = fs_->GetDirtyDataPageList().TakePages(2);

    ASSERT_EQ(pages[0]->GetKey(), 1ULL);
    ASSERT_EQ(fs_->GetDirtyDataPageList().Size(), 1U);
    ASSERT_TRUE(pages[0]->ClearDirtyForIo());
  }

  // Release the lock.
  locked_page.reset();

  // Try to take 2 Pages from the list
  {
    auto pages = fs_->GetDirtyDataPageList().TakePages(2);
    ASSERT_EQ(pages[0]->GetKey(), 0ULL);
    ASSERT_EQ(fs_->GetDirtyDataPageList().Size(), 0U);
    ASSERT_TRUE(pages[0]->ClearDirtyForIo());
  }

  vn->Close();
  vn = nullptr;
}

TEST_F(DirtyPageListTest, ResetFileCache) {
  fbl::RefPtr<fs::Vnode> test_file;
  root_dir_->Create("test", S_IFREG, &test_file);
  fbl::RefPtr<f2fs::File> vn = fbl::RefPtr<f2fs::File>::Downcast(std::move(test_file));
  char buf[kPageSize];

  // Make dirty Page
  FileTester::AppendToFile(vn.get(), buf, kPageSize);

  ASSERT_EQ(fs_->GetDirtyDataPageList().Size(), 1U);

  Page *raw_page;
  {
    LockedPage locked_page;
    vn->GrabCachePage(0, &locked_page);
    ASSERT_EQ(locked_page->IsDirty(), true);
    ASSERT_EQ(locked_page->InTreeContainer(), true);
    ASSERT_EQ(locked_page->InListContainer(), true);
    raw_page = locked_page.get();
  }

  fs_->GetDirtyDataPageList().Reset();
  raw_page->GetFileCache().Reset();
  ASSERT_EQ(fs_->GetDirtyDataPageList().Size(), 0U);

  vn->Close();
  vn = nullptr;
}

TEST_F(DirtyPageListTest, ResetDirtyPageList) {
  fbl::RefPtr<fs::Vnode> test_file;
  root_dir_->Create("test", S_IFREG, &test_file);
  fbl::RefPtr<f2fs::File> vn = fbl::RefPtr<f2fs::File>::Downcast(std::move(test_file));
  char buf[kPageSize];

  // Make dirty Page
  FileTester::AppendToFile(vn.get(), buf, kPageSize);

  ASSERT_EQ(fs_->GetDirtyDataPageList().Size(), 1U);

  {
    LockedPage locked_page;
    vn->GrabCachePage(0, &locked_page);
    ASSERT_EQ(locked_page->IsDirty(), true);
    ASSERT_EQ(locked_page->InTreeContainer(), true);
    ASSERT_EQ(locked_page->InListContainer(), true);
  }

  ASSERT_EQ(fs_->GetDirtyDataPageList().Size(), 1U);
  fs_->GetDirtyDataPageList().Reset();
  ASSERT_EQ(fs_->GetDirtyDataPageList().Size(), 0U);

  vn->Close();
  vn = nullptr;
}

}  // namespace
}  // namespace f2fs
