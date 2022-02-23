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
  page->SetWriteback();
  std::thread thread([&]() {
    page->ClearWriteback();
    page->Lock();
    ASSERT_EQ(page->IsWriteback(), true);
    page->ClearWriteback();
  });

  // Wait for |thread| to run.
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

TEST_F(FileCacheTest, WritebackOperation) {
  fbl::RefPtr<fs::Vnode> test_file;
  root_dir_->Create("test", S_IFREG, &test_file);
  fbl::RefPtr<f2fs::File> vn = fbl::RefPtr<f2fs::File>::Downcast(std::move(test_file));
  fbl::RefPtr<Page> page;
  char buf[kPageSize];

  // |vn| should not have any dirty Pages.
  ASSERT_EQ(vn->GetDirtyPageCount(), 0);
  FileTester::AppendToFile(vn.get(), buf, kPageSize);
  FileTester::AppendToFile(vn.get(), buf, kPageSize);
  // Get the Page of 2nd block.
  vn->GrabCachePage(1, &page);
  ASSERT_EQ(vn->GetDirtyPageCount(), 2);

  auto key = page->GetKey();
  WritebackOperation op = {.start = 0,
                           .end = 2,
                           .to_write = 2,
                           .bSync = false,
                           .if_page = [&key](fbl::RefPtr<Page> &page) {
                             if (page->GetKey() <= key) {
                               return ZX_OK;
                             }
                             return ZX_ERR_NEXT;
                           }};

  // Request writeback for dirty Pages. The Page of 1st block should be written out.
  ASSERT_EQ(vn->Writeback(op), 1UL);
  // Writeback() should not touch active Pages such as |page|.
  ASSERT_EQ(vn->GetDirtyPageCount(), 1);
  ASSERT_EQ(op.to_write, 1UL);
  ASSERT_EQ(fs_->GetSuperblockInfo().GetPageCount(CountType::kWriteback), 1);
  ASSERT_EQ(fs_->GetSuperblockInfo().GetPageCount(CountType::kDirtyData), 1);
  ASSERT_EQ(page->IsWriteback(), false);
  ASSERT_EQ(page->IsDirty(), true);
  Page::PutPage(std::move(page), true);

  key = 0;
  // Request writeback for dirty Pages, but there is no Page meeting op.if_page.
  ASSERT_EQ(vn->Writeback(op), 0UL);
  key = 1;
  // Now, 2nd Page meets op.if_page.
  ASSERT_EQ(vn->Writeback(op), 1UL);
  ASSERT_EQ(op.to_write, 0UL);
  ASSERT_EQ(vn->GetDirtyPageCount(), 0);
  ASSERT_EQ(fs_->GetSuperblockInfo().GetPageCount(CountType::kWriteback), 2);
  ASSERT_EQ(fs_->GetSuperblockInfo().GetPageCount(CountType::kDirtyData), 0);

  // Request sync. writeback.
  op.bSync = true;
  // No dirty Pages to be written.
  // All writeback Pages should be clean.
  ASSERT_EQ(vn->Writeback(op), 0UL);
  ASSERT_EQ(fs_->GetSuperblockInfo().GetPageCount(CountType::kWriteback), 0);

  // Do not release clean Pages
  op.bReleasePages = false;
  // It should not release any clean Pages.
  ASSERT_EQ(vn->Writeback(op), 0UL);
  // Pages at 1st and 2nd blocks should be uptodate
  vn->GrabCachePage(0, &page);
  ASSERT_EQ(page->IsUptodate(), true);
  Page::PutPage(std::move(page), true);
  vn->GrabCachePage(1, &page);
  ASSERT_EQ(page->IsUptodate(), true);
  Page::PutPage(std::move(page), true);

  // Release clean Pages
  op.bReleasePages = true;
  // It should release and evict clean Pages from FileCache.
  ASSERT_EQ(vn->Writeback(op), 0UL);
  // There is no uptodate Page.
  vn->GrabCachePage(0, &page);
  ASSERT_EQ(page->IsUptodate(), false);
  Page::PutPage(std::move(page), true);
  vn->GrabCachePage(1, &page);
  ASSERT_EQ(page->IsUptodate(), false);
  Page::PutPage(std::move(page), true);

  vn->Close();
  vn = nullptr;
}

TEST_F(FileCacheTest, Recycle) {
  fbl::RefPtr<fs::Vnode> test_file;
  root_dir_->Create("test", S_IFREG, &test_file);
  fbl::RefPtr<f2fs::File> vn = fbl::RefPtr<f2fs::File>::Downcast(std::move(test_file));
  char buf[kPageSize];

  FileTester::AppendToFile(vn.get(), buf, kPageSize);

  fbl::RefPtr<Page> page;
  vn->GrabCachePage(0, &page);
  ASSERT_EQ(page->IsDirty(), true);
  Page *raw_page = page.get();
  Page::PutPage(std::move(page), true);

  page = fbl::ImportFromRawPtr(raw_page);
  // ref_count should be set to one in Page::fbl_recycle().
  ASSERT_EQ(page->IsLastReference(), true);
  // Leak it to keep alive in FileCache.
  raw_page = fbl::ExportToRawPtr(&page);
  FileCache &cache = raw_page->GetFileCache();

  raw_page->Lock();
  // Test FileCache::GetPage() and FileCache::Downgrade() with multiple threads
  std::thread thread1([&]() {
    int i = 1000;
    while (--i) {
      fbl::RefPtr<Page> page;
      vn->GrabCachePage(0, &page);
      ASSERT_EQ(page->IsDirty(), true);
      Page::PutPage(std::move(page), true);
    }
  });

  std::thread thread2([&]() {
    int i = 1000;
    while (--i) {
      fbl::RefPtr<Page> page;
      vn->GrabCachePage(0, &page);
      ASSERT_EQ(page->IsDirty(), true);
      Page::PutPage(std::move(page), true);
    }
  });
  // Start threads.
  raw_page->Unlock();
  thread1.join();
  thread2.join();

  // Test FileCache::Downgrade() and FileCache::Reset() with multiple threads
  // Before FileCache::Reset(), a caller should ensure that there is no dirty Pages in FileCache.
  vn->GrabCachePage(0, &page);
  page->Invalidate();
  Page::PutPage(std::move(page), true);

  std::thread thread_get_page([&]() {
    bool bStop = false;
    std::thread thread_reset([&]() {
      while (!bStop) {
        cache.Reset();
      }
    });

    int i = 1000;
    while (--i) {
      fbl::RefPtr<Page> page;
      vn->GrabCachePage(0, &page);
      ASSERT_EQ(page->IsUptodate(), false);
      Page::PutPage(std::move(page), true);
    }
    bStop = true;
    thread_reset.join();
  });

  thread_get_page.join();

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
    ASSERT_EQ(page->Zero(0, 1), ZX_OK);
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
