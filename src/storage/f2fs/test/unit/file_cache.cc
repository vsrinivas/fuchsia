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

using FileCacheTest = F2fsFakeDevTestFixture;

TEST_F(FileCacheTest, WaitOnLock) {
  fbl::RefPtr<fs::Vnode> test_file;
  root_dir_->Create("test", S_IFREG, &test_file);
  fbl::RefPtr<f2fs::File> vn = fbl::RefPtr<f2fs::File>::Downcast(std::move(test_file));
  fbl::RefPtr<Page> page;

  vn->GrabCachePage(0, &page);
  ASSERT_EQ(page->TryLock(), true);
  std::thread thread([&]() { page->Unlock(); });
  // Wait for |thread| to unlock |page|.
  page->Lock();
  thread.join();
  Page::PutPage(std::move(page), true);

  vn->Close();
  vn = nullptr;
}

TEST_F(FileCacheTest, WaitOnWriteback) {
  fbl::RefPtr<fs::Vnode> test_file;
  root_dir_->Create("test", S_IFREG, &test_file);
  fbl::RefPtr<f2fs::File> vn = fbl::RefPtr<f2fs::File>::Downcast(std::move(test_file));
  fbl::RefPtr<Page> page;

  vn->GrabCachePage(0, &page);
  std::thread thread([&]() {
    page->Lock();
    ASSERT_EQ(page->IsWriteback(), true);
    page->ClearWriteback();
  });

  page->WaitOnWriteback();
  page->SetWriteback();
  ASSERT_EQ(page->IsWriteback(), true);
  page->Unlock();
  // Wait for |thread| to clear kPageWriteback.
  page->WaitOnWriteback();
  ASSERT_EQ(page->IsWriteback(), false);
  thread.join();
  Page::PutPage(std::move(page), true);

  vn->Close();
  vn = nullptr;
}

TEST_F(FileCacheTest, Map) {
  fbl::RefPtr<fs::Vnode> test_file;
  root_dir_->Create("test", S_IFREG, &test_file);
  fbl::RefPtr<f2fs::File> vn = fbl::RefPtr<f2fs::File>::Downcast(std::move(test_file));
  fbl::RefPtr<Page> page;

  vn->GrabCachePage(0, &page);
  // Set kPageUptodate to keep |page| in FileCache.
  page->SetUptodate();
  // Since FileCache hold the last reference to |page|, it is safe to use |raw_ptr| here.
  Page *raw_ptr = page.get();
  // If kDirtyPage is set, FileCache keeps the mapping of |page| since writeback will use it soon.
  // Otherwise, |page| is unmapped when there is no reference except for FileCache.
  Page::PutPage(std::move(page), true);
  // After PutPage(), |page| should be unmapped since kPageDirty is clear and there is no reference
  // but for FileCache.
  ASSERT_EQ(raw_ptr->IsLocked(), false);
  ASSERT_EQ(raw_ptr->IsMapped(), false);

  vn->GrabCachePage(0, &page);
  // |page| should be mapped as a new reference is added.
  ASSERT_EQ(raw_ptr->IsMapped(), true);
  ASSERT_EQ(page->IsLocked(), true);
  Page::PutPage(std::move(page), true);

  vn->Close();
  vn = nullptr;
}

TEST_F(FileCacheTest, Basic) {
  fbl::RefPtr<fs::Vnode> test_file;
  root_dir_->Create("test", S_IFREG, &test_file);
  fbl::RefPtr<f2fs::File> vn = fbl::RefPtr<f2fs::File>::Downcast(std::move(test_file));

  uint8_t buf[kPageSize];
  const uint16_t nblocks = 256;

  // All pages should not be uptodated.
  for (uint16_t i = 0; i < nblocks; ++i) {
    fbl::RefPtr<Page> page;
    uint8_t r_buf[kPageSize], w_buf[kPageSize];
    vn->GrabCachePage(i, &page);
    // A newly created page should have kPageUptodate/kPageDirty/kPageWriteback flags clear.
    ASSERT_EQ(page->IsUptodate(), false);
    ASSERT_EQ(page->IsDirty(), false);
    ASSERT_EQ(page->IsMapped(), true);
    ASSERT_EQ(page->IsAllocated(), true);
    ASSERT_EQ(page->IsWriteback(), false);
    ASSERT_EQ(page->IsLocked(), true);

    // Sanity checks for storage::BlockBuffer.
    ASSERT_EQ(page->Vmo(), ZX_HANDLE_INVALID);
    ASSERT_EQ(page->Data(0), page->GetAddress());
    ASSERT_EQ(page->capacity(), page->BlockSize() / kPageSize);
    ASSERT_EQ(page->GetVnodeId(), vn->GetKey());

    // Sanity checks for interfaces to Page::vmo_.
    memset(w_buf, i, kPageSize);
    ASSERT_EQ(page->VmoWrite(w_buf, 0, kPageSize), ZX_OK);
    ASSERT_EQ(page->VmoRead(r_buf, 0, kPageSize), ZX_OK);
    ASSERT_EQ(memcmp(r_buf, w_buf, kPageSize), 0);
    page->Zero(0, 1);
    ASSERT_EQ(page->VmoRead(r_buf, 0, kPageSize), ZX_OK);
    memset(w_buf, 0, kPageSize);
    ASSERT_EQ(memcmp(r_buf, w_buf, kPageSize), 0);

    const void *ptr1 = page->Data(0);
    void *ptr2 = page->Data(0);
    ASSERT_EQ(ptr1, ptr2);
    ASSERT_EQ(page->Data(page->capacity()), nullptr);

    Page::PutPage(std::move(page), true);
  }

  // Append |nblocks| * |kPageSize|.
  // Each block is filled with its block offset.
  for (uint16_t i = 0; i < nblocks; ++i) {
    memset(buf, i, kPageSize);
    FileTester::AppendToFile(vn.get(), buf, kPageSize);
  }

  // All pages should be uptodated and dirty.
  for (uint16_t i = 0; i < nblocks; ++i) {
    fbl::RefPtr<Page> page;
    memset(buf, i, kPageSize);
    vn->GrabCachePage(i, &page);
    ASSERT_EQ(page->IsUptodate(), true);
    ASSERT_EQ(page->IsDirty(), true);
    ASSERT_EQ(memcmp(buf, page->GetAddress(), kPageSize), 0);
    Page::PutPage(std::move(page), true);
  }

  // Write out some dirty pages
  WritebackOperation op = {.end = nblocks / 2, .bSync = true};
  vn->Writeback(op);

  // Check if each page has a correct dirty flag.
  for (size_t i = 0; i < nblocks; ++i) {
    fbl::RefPtr<Page> page;
    vn->GrabCachePage(i, &page);
    ASSERT_EQ(page->IsUptodate(), true);
    if (i < nblocks / 2) {
      ASSERT_EQ(page->IsDirty(), false);
    } else {
      ASSERT_EQ(page->IsDirty(), true);
    }
    Page::PutPage(std::move(page), true);
  }

  vn->Close();
  vn = nullptr;
}

TEST_F(FileCacheTest, Truncate) {
  fbl::RefPtr<fs::Vnode> test_file;
  root_dir_->Create("test", S_IFREG, &test_file);
  fbl::RefPtr<f2fs::File> vn = fbl::RefPtr<f2fs::File>::Downcast(std::move(test_file));

  uint8_t buf[kPageSize];
  const uint16_t nblocks = 256;

  // Append |nblocks| * |kPageSize|.
  // Each block is filled with its block offset.
  for (uint16_t i = 0; i < nblocks; ++i) {
    memset(buf, i, kPageSize);
    FileTester::AppendToFile(vn.get(), buf, kPageSize);
  }

  // All pages should be uptodated and dirty.
  for (uint16_t i = 0; i < nblocks; ++i) {
    fbl::RefPtr<Page> page;
    vn->GrabCachePage(i, &page);
    ASSERT_EQ(page->IsUptodate(), true);
    ASSERT_EQ(page->IsDirty(), true);
    Page::PutPage(std::move(page), true);
  }

  // Truncate the size of |vn| to the half.
  pgoff_t start = nblocks / 2 * kPageSize;
  vn->TruncateBlocks(start);

  // Check if each page has correct flags.
  for (size_t i = 0; i < nblocks; ++i) {
    fbl::RefPtr<Page> page;
    vn->GrabCachePage(i, &page);
    DnodeOfData dn;
    NodeManager::SetNewDnode(dn, vn.get(), nullptr, nullptr, 0);
    fs_->GetNodeManager().GetDnodeOfData(dn, i, kRdOnlyNode);
    if (i >= start / kPageSize) {
      ASSERT_EQ(page->IsDirty(), false);
      ASSERT_EQ(page->IsUptodate(), false);
      ASSERT_EQ(dn.data_blkaddr, kNullAddr);
    } else {
      ASSERT_EQ(page->IsDirty(), true);
      ASSERT_EQ(page->IsUptodate(), true);
      ASSERT_EQ(dn.data_blkaddr, kNewAddr);
    }
    F2fsPutDnode(&dn);
    Page::PutPage(std::move(page), true);
  }

  --start;
  // Punch a hole at start
  vn->TruncateHole(start, start + 1);

  fbl::RefPtr<Page> page;
  vn->GrabCachePage(start, &page);
  DnodeOfData dn;
  NodeManager::SetNewDnode(dn, vn.get(), nullptr, nullptr, 0);
  fs_->GetNodeManager().GetDnodeOfData(dn, start, kRdOnlyNode);
  // |page| for the hole should be invalidated.
  ASSERT_EQ(page->IsDirty(), false);
  ASSERT_EQ(page->IsUptodate(), false);
  ASSERT_EQ(dn.data_blkaddr, kNullAddr);
  F2fsPutDnode(&dn);
  Page::PutPage(std::move(page), true);

  vn->Close();
  vn = nullptr;
}

}  // namespace
}  // namespace f2fs
