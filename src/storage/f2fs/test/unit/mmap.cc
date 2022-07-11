// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <unordered_set>

#include <gtest/gtest.h>
#include <safemath/checked_math.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/storage/f2fs/f2fs.h"
#include "src/storage/f2fs/f2fs_layout.h"
#include "src/storage/f2fs/f2fs_types.h"
#include "unit_lib.h"

namespace f2fs {
namespace {

using MmapTest = F2fsFakeDevTestFixture;

TEST_F(MmapTest, GetVmo) {
  srand(testing::UnitTest::GetInstance()->random_seed());

  fbl::RefPtr<fs::Vnode> test_fs_vnode;
  std::string file_name("mmap_getvmo_test");
  ASSERT_EQ(root_dir_->Create(file_name, S_IFREG, &test_fs_vnode), ZX_OK);
  fbl::RefPtr<VnodeF2fs> test_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(test_fs_vnode));
  File *test_file_ptr = static_cast<File *>(test_vnode.get());

  uint8_t write_buf[PAGE_SIZE];
  for (uint8_t &character : write_buf) {
    character = static_cast<uint8_t>(rand());
  }

  FileTester::AppendToFile(test_file_ptr, write_buf, PAGE_SIZE);

  zx::vmo vmo;
  uint8_t read_buf[PAGE_SIZE];
  ASSERT_EQ(test_vnode->GetVmo(fuchsia_io::wire::VmoFlags::kRead, &vmo), ZX_OK);
  vmo.read(read_buf, 0, PAGE_SIZE);
  vmo.reset();
  loop_.RunUntilIdle();

  ASSERT_EQ(memcmp(read_buf, write_buf, PAGE_SIZE), 0);

  test_vnode->Close();
  test_vnode.reset();
}

TEST_F(MmapTest, GetVmoSize) {
  srand(testing::UnitTest::GetInstance()->random_seed());

  fbl::RefPtr<fs::Vnode> test_fs_vnode;
  std::string file_name("mmap_getvmo_size_test");
  ASSERT_EQ(root_dir_->Create(file_name, S_IFREG, &test_fs_vnode), ZX_OK);
  fbl::RefPtr<VnodeF2fs> test_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(test_fs_vnode));
  File *test_file_ptr = static_cast<File *>(test_vnode.get());

  uint8_t write_buf[PAGE_SIZE];
  for (uint8_t &character : write_buf) {
    character = static_cast<uint8_t>(rand());
  }

  FileTester::AppendToFile(test_file_ptr, write_buf, PAGE_SIZE);

  // Create paged_vmo
  zx::vmo vmo;
  ASSERT_EQ(test_vnode->GetVmo(fuchsia_io::wire::VmoFlags::kRead, &vmo), ZX_OK);
  vmo.reset();

  // Increase file size
  FileTester::AppendToFile(test_file_ptr, write_buf, PAGE_SIZE);

  // Get new Private VMO, but paged_vmo size is not increased.
  zx::vmo private_vmo;
  ASSERT_EQ(test_vnode->GetVmo(fuchsia_io::wire::VmoFlags::kRead, &private_vmo), ZX_OK);
  private_vmo.reset();

  // Get new Shared VMO, but paged_vmo size is not increased.
  zx::vmo shared_vmo;
  ASSERT_EQ(test_vnode->GetVmo(
                fuchsia_io::wire::VmoFlags::kSharedBuffer | fuchsia_io::wire::VmoFlags::kRead,
                &shared_vmo),
            ZX_OK);
  shared_vmo.reset();
  loop_.RunUntilIdle();

  test_vnode->Close();
  test_vnode.reset();
}

TEST_F(MmapTest, GetVmoZeroSize) {
  fbl::RefPtr<fs::Vnode> test_fs_vnode;
  std::string file_name("mmap_getvmo_zero_size_test");
  ASSERT_EQ(root_dir_->Create(file_name, S_IFREG, &test_fs_vnode), ZX_OK);
  fbl::RefPtr<VnodeF2fs> test_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(test_fs_vnode));

  zx::vmo vmo;
  uint8_t read_buf[PAGE_SIZE];
  ASSERT_EQ(test_vnode->GetVmo(fuchsia_io::wire::VmoFlags::kRead, &vmo), ZX_OK);
  vmo.read(read_buf, 0, PAGE_SIZE);
  vmo.reset();
  loop_.RunUntilIdle();

  test_vnode->Close();
  test_vnode.reset();
}

TEST_F(MmapTest, GetVmoOnDirectory) {
  fbl::RefPtr<fs::Vnode> test_fs_vnode;
  std::string file_name("mmap_getvmo_dir_test");
  ASSERT_EQ(root_dir_->Create(file_name, S_IFDIR, &test_fs_vnode), ZX_OK);
  fbl::RefPtr<VnodeF2fs> test_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(test_fs_vnode));

  zx::vmo vmo;
  ASSERT_EQ(test_vnode->GetVmo(fuchsia_io::wire::VmoFlags::kRead, &vmo), ZX_ERR_NOT_SUPPORTED);
  vmo.reset();
  loop_.RunUntilIdle();

  test_vnode->Close();
  test_vnode.reset();
}

TEST_F(MmapTest, GetVmoTruncatePartial) {
  fbl::RefPtr<fs::Vnode> test_fs_vnode;
  std::string file_name("mmap_getvmo_truncate_partial_test");
  ASSERT_EQ(root_dir_->Create(file_name, S_IFREG, &test_fs_vnode), ZX_OK);
  fbl::RefPtr<VnodeF2fs> test_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(test_fs_vnode));
  File *test_file_ptr = static_cast<File *>(test_vnode.get());

  constexpr size_t kPageCount = 5;
  constexpr size_t kBufferSize = kPageCount * PAGE_SIZE;
  uint8_t write_buf[kBufferSize];
  for (size_t i = 0; i < kPageCount; ++i) {
    memset(write_buf + (i * PAGE_SIZE), static_cast<int>(i), PAGE_SIZE);
  }
  FileTester::AppendToFile(test_file_ptr, write_buf, kBufferSize);
  ASSERT_EQ(test_vnode->GetSize(), kBufferSize);

  zx::vmo vmo;
  ASSERT_EQ(
      test_vnode->GetVmo(
          fuchsia_io::wire::VmoFlags::kSharedBuffer | fuchsia_io::wire::VmoFlags::kRead, &vmo),
      ZX_OK);

  uint8_t read_buf[kBufferSize];
  uint8_t zero_buf[kBufferSize];
  memset(zero_buf, 0, kBufferSize);

  // Truncate partial page
  size_t zero_size = PAGE_SIZE / 4;
  size_t truncate_size = kBufferSize - zero_size;
  test_vnode->Truncate(truncate_size);
  ASSERT_EQ(test_vnode->GetSize(), truncate_size);
  vmo.read(read_buf, 0, kBufferSize);
  ASSERT_EQ(memcmp(read_buf, write_buf, truncate_size), 0);
  ASSERT_EQ(memcmp(read_buf + truncate_size, zero_buf, zero_size), 0);

  zero_size = PAGE_SIZE / 2;
  truncate_size = kBufferSize - zero_size;
  test_vnode->Truncate(truncate_size);
  ASSERT_EQ(test_vnode->GetSize(), truncate_size);
  vmo.read(read_buf, 0, kBufferSize);
  ASSERT_EQ(memcmp(read_buf, write_buf, truncate_size), 0);
  ASSERT_EQ(memcmp(read_buf + truncate_size, zero_buf, zero_size), 0);

  vmo.reset();
  loop_.RunUntilIdle();

  test_vnode->Close();
  test_vnode.reset();
}

TEST_F(MmapTest, GetVmoTruncatePage) {
  fbl::RefPtr<fs::Vnode> test_fs_vnode;
  std::string file_name("mmap_getvmo_truncate_page_test");
  ASSERT_EQ(root_dir_->Create(file_name, S_IFREG, &test_fs_vnode), ZX_OK);
  fbl::RefPtr<VnodeF2fs> test_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(test_fs_vnode));
  File *test_file_ptr = static_cast<File *>(test_vnode.get());

  constexpr size_t kPageCount = 5;
  constexpr size_t kBufferSize = kPageCount * PAGE_SIZE;
  uint8_t write_buf[kBufferSize];
  for (size_t i = 0; i < kPageCount; ++i) {
    memset(write_buf + (i * PAGE_SIZE), static_cast<int>(i), PAGE_SIZE);
  }
  FileTester::AppendToFile(test_file_ptr, write_buf, kBufferSize);
  ASSERT_EQ(test_vnode->GetSize(), kBufferSize);

  zx::vmo vmo;
  ASSERT_EQ(
      test_vnode->GetVmo(
          fuchsia_io::wire::VmoFlags::kSharedBuffer | fuchsia_io::wire::VmoFlags::kRead, &vmo),
      ZX_OK);

  uint8_t read_buf[kBufferSize];
  uint8_t zero_buf[kBufferSize];
  memset(zero_buf, 0, kBufferSize);

  // Truncate one page
  size_t zero_size = PAGE_SIZE;
  size_t truncate_size = kBufferSize - zero_size;
  ASSERT_EQ(test_vnode->Truncate(truncate_size), ZX_OK);
  ASSERT_EQ(test_vnode->GetSize(), truncate_size);
  vmo.read(read_buf, 0, kBufferSize);
  ASSERT_EQ(memcmp(read_buf, write_buf, truncate_size), 0);
  ASSERT_EQ(memcmp(read_buf + truncate_size, zero_buf, PAGE_SIZE), 0);

  // Truncate two pages
  zero_size = static_cast<size_t>(PAGE_SIZE) * 2;
  truncate_size = kBufferSize - zero_size;
  ASSERT_EQ(test_vnode->Truncate(truncate_size), ZX_OK);
  ASSERT_EQ(test_vnode->GetSize(), truncate_size);
  vmo.read(read_buf, 0, kBufferSize);
  ASSERT_EQ(memcmp(read_buf, write_buf, truncate_size), 0);
  ASSERT_EQ(memcmp(read_buf + truncate_size, zero_buf, zero_size), 0);

  vmo.reset();
  loop_.RunUntilIdle();

  test_vnode->Close();
  test_vnode.reset();
}

TEST_F(MmapTest, GetVmoException) {
  fbl::RefPtr<fs::Vnode> test_fs_vnode;
  std::string file_name("mmap_getvmo_exception_test");
  ASSERT_EQ(root_dir_->Create(file_name, S_IFREG, &test_fs_vnode), ZX_OK);
  fbl::RefPtr<VnodeF2fs> test_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(test_fs_vnode));

  zx::vmo vmo;

  // Execute flag
  fuchsia_io::wire::VmoFlags flags;
  flags = fuchsia_io::wire::VmoFlags::kExecute;
  ASSERT_EQ(test_vnode->GetVmo(flags, &vmo), ZX_ERR_NOT_SUPPORTED);

  // Shared write flag
  flags = fuchsia_io::wire::VmoFlags::kSharedBuffer | fuchsia_io::wire::VmoFlags::kWrite;
  ASSERT_EQ(test_vnode->GetVmo(flags, &vmo), ZX_ERR_NOT_SUPPORTED);
  vmo.reset();
  loop_.RunUntilIdle();

  test_vnode->Close();
  test_vnode.reset();
}

TEST_F(MmapTest, VmoRead) {
  srand(testing::UnitTest::GetInstance()->random_seed());

  fbl::RefPtr<fs::Vnode> test_fs_vnode;
  std::string file_name("mmap_vmoread_test");
  ASSERT_EQ(root_dir_->Create(file_name, S_IFREG, &test_fs_vnode), ZX_OK);
  fbl::RefPtr<VnodeF2fs> test_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(test_fs_vnode));
  File *test_file_ptr = static_cast<File *>(test_vnode.get());

  uint8_t write_buf[PAGE_SIZE];
  for (uint8_t &character : write_buf) {
    character = static_cast<uint8_t>(rand());
  }

  FileTester::AppendToFile(test_file_ptr, write_buf, PAGE_SIZE);

  zx::vmo vmo;
  uint8_t read_buf[PAGE_SIZE];
  ASSERT_EQ(test_vnode->GetVmo(fuchsia_io::wire::VmoFlags::kRead, &vmo), ZX_OK);
  test_vnode->VmoRead(0, PAGE_SIZE);
  vmo.read(read_buf, 0, PAGE_SIZE);
  vmo.reset();
  loop_.RunUntilIdle();

  ASSERT_EQ(memcmp(read_buf, write_buf, PAGE_SIZE), 0);

  test_vnode->Close();
  test_vnode.reset();
}

TEST_F(MmapTest, VmoReadException) {
  srand(testing::UnitTest::GetInstance()->random_seed());

  fbl::RefPtr<fs::Vnode> test_fs_vnode;
  std::string file_name("mmap_vmoread_exception_test");
  ASSERT_EQ(root_dir_->Create(file_name, S_IFREG, &test_fs_vnode), ZX_OK);
  fbl::RefPtr<VnodeF2fs> test_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(test_fs_vnode));
  File *test_file_ptr = static_cast<File *>(test_vnode.get());

  uint8_t write_buf[PAGE_SIZE];
  for (uint8_t &character : write_buf) {
    character = static_cast<uint8_t>(rand());
  }

  FileTester::AppendToFile(test_file_ptr, write_buf, PAGE_SIZE);

  zx::vmo vmo;
  uint8_t read_buf[PAGE_SIZE];
  ASSERT_EQ(test_vnode->GetVmo(fuchsia_io::wire::VmoFlags::kRead, &vmo), ZX_OK);
  vmo.reset();
  loop_.RunUntilIdle();
  test_vnode->VmoRead(0, PAGE_SIZE);

  ASSERT_NE(memcmp(read_buf, write_buf, PAGE_SIZE), 0);

  test_vnode->Close();
  test_vnode.reset();
}

TEST_F(MmapTest, VmoReadSizeException) {
  srand(testing::UnitTest::GetInstance()->random_seed());

  fbl::RefPtr<fs::Vnode> test_fs_vnode;
  std::string file_name("mmap_getvmo_size_exception_test");
  ASSERT_EQ(root_dir_->Create(file_name, S_IFREG, &test_fs_vnode), ZX_OK);
  fbl::RefPtr<VnodeF2fs> test_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(test_fs_vnode));
  File *test_file_ptr = static_cast<File *>(test_vnode.get());

  uint8_t write_buf[PAGE_SIZE];
  for (uint8_t &character : write_buf) {
    character = static_cast<uint8_t>(rand());
  }

  FileTester::AppendToFile(test_file_ptr, write_buf, PAGE_SIZE);

  zx::vmo vmo;
  uint8_t read_buf[PAGE_SIZE];
  ASSERT_EQ(test_vnode->GetVmo(fuchsia_io::wire::VmoFlags::kRead, &vmo), ZX_OK);
  test_vnode->VmoRead(0, PAGE_SIZE);
  vmo.read(read_buf, 0, PAGE_SIZE);
  ASSERT_EQ(memcmp(read_buf, write_buf, PAGE_SIZE), 0);

  // Append to file after mmap
  FileTester::AppendToFile(test_file_ptr, write_buf, PAGE_SIZE);
  memset(read_buf, 0, PAGE_SIZE);
  test_vnode->VmoRead(PAGE_SIZE, PAGE_SIZE);
  vmo.read(read_buf, PAGE_SIZE, PAGE_SIZE);
  ASSERT_NE(memcmp(read_buf, write_buf, PAGE_SIZE), 0);

  vmo.reset();
  loop_.RunUntilIdle();

  test_vnode->Close();
  test_vnode.reset();
}

TEST_F(MmapTest, AvoidPagedVmoRaceCondition) {
  srand(testing::UnitTest::GetInstance()->random_seed());

  fbl::RefPtr<fs::Vnode> test_fs_vnode;
  std::string file_name("mmap_avoid_paged_vmo_race_condition_test");
  ASSERT_EQ(root_dir_->Create(file_name, S_IFREG, &test_fs_vnode), ZX_OK);
  fbl::RefPtr<VnodeF2fs> test_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(test_fs_vnode));
  File *test_file_ptr = static_cast<File *>(test_vnode.get());

  uint8_t write_buf[PAGE_SIZE];
  for (uint8_t &character : write_buf) {
    character = static_cast<uint8_t>(rand());
  }

  FileTester::AppendToFile(test_file_ptr, write_buf, PAGE_SIZE);

  // Clone a VMO from pager-backed VMO
  zx::vmo vmo;
  ASSERT_EQ(test_vnode->GetVmo(fuchsia_io::wire::VmoFlags::kRead, &vmo), ZX_OK);

  // Close the cloned VMO
  vmo.reset();
  loop_.RunUntilIdle();

  // Make sure pager-backed VMO is not freed
  ASSERT_EQ(test_vnode->HasPagedVmo(), true);

  // Request a page fault assuming a race condition
  test_vnode->VmoRead(0, PAGE_SIZE);

  // Make sure pager-backed VMO is not freed
  ASSERT_EQ(test_vnode->HasPagedVmo(), true);

  test_vnode->Close();
  test_vnode.reset();
}

TEST_F(MmapTest, ReleasePagedVmoInVnodeRecycle) {
  srand(testing::UnitTest::GetInstance()->random_seed());

  fbl::RefPtr<fs::Vnode> test_fs_vnode;
  std::string file_name("mmap_release_paged_vmo_in_vnode_recycle_test");
  ASSERT_EQ(root_dir_->Create(file_name, S_IFREG, &test_fs_vnode), ZX_OK);
  fbl::RefPtr<VnodeF2fs> test_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(test_fs_vnode));
  File *test_file_ptr = static_cast<File *>(test_vnode.get());

  uint8_t write_buf[PAGE_SIZE];
  for (uint8_t &character : write_buf) {
    character = static_cast<uint8_t>(rand());
  }

  FileTester::AppendToFile(test_file_ptr, write_buf, PAGE_SIZE);

  // Sync to remove vnode from dirty list.
  WritebackOperation op;
  test_file_ptr->Writeback(op);
  fs_->SyncFs();

  zx::vmo vmo;
  ASSERT_EQ(test_vnode->GetVmo(fuchsia_io::wire::VmoFlags::kRead, &vmo), ZX_OK);

  // Pager-backed VMO is not freed becasue vnode is in vnode cache
  vmo.reset();
  loop_.RunUntilIdle();

  // Make sure pager-backed VMO is not freed
  ASSERT_EQ(test_vnode->HasPagedVmo(), true);

  // Release pager-backed VMO directly
  test_vnode->ReleasePagedVmo();

  // Make sure pager-backed VMO is freed
  ASSERT_EQ(test_vnode->HasPagedVmo(), false);

  ASSERT_EQ(test_vnode->GetVmo(fuchsia_io::wire::VmoFlags::kRead, &vmo), ZX_OK);

  // Pager-backed VMO has been reallocated
  ASSERT_EQ(test_vnode->HasPagedVmo(), true);

  vmo.reset();
  loop_.RunUntilIdle();

  // Pager-backed VMO is released in vnode recycle
  VnodeF2fs *vnode_ptr = test_vnode.get();
  test_vnode->Close();
  test_vnode.reset();

  // Make sure pager-backed VMO is freed
  ASSERT_EQ(vnode_ptr->HasPagedVmo(), false);
}

}  // namespace
}  // namespace f2fs
