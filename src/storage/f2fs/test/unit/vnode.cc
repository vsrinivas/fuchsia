// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <numeric>

#include <gtest/gtest.h>
#include <safemath/checked_math.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/storage/f2fs/f2fs.h"
#include "unit_lib.h"

namespace f2fs {
namespace {

using VnodeTest = F2fsFakeDevTestFixture;

template <typename T>
void VgetFaultInjetionAndTest(F2fs &fs, Dir &root_dir, std::string_view name, T fault_injection,
                              zx_status_t expected_status) {
  FileTester::CreateChild(&root_dir, S_IFDIR, name);
  fbl::RefPtr<fs::Vnode> dir_raw_vnode;
  FileTester::Lookup(&root_dir, name, &dir_raw_vnode);
  fbl::RefPtr<VnodeF2fs> test_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(dir_raw_vnode));
  nid_t nid = test_vnode->GetKey();
  ASSERT_EQ(test_vnode->Close(), ZX_OK);
  test_vnode = nullptr;

  ASSERT_EQ(VnodeF2fs::Vget(&fs, nid, &test_vnode), ZX_OK);
  ASSERT_EQ(
      test_vnode->Open(test_vnode->ValidateOptions(fs::VnodeConnectionOptions()).value(), nullptr),
      ZX_OK);
  ASSERT_EQ(test_vnode->Close(), ZX_OK);

  // fault injection
  {
    LockedPage node_page;
    ASSERT_EQ(fs.GetNodeManager().GetNodePage(nid, &node_page), ZX_OK);
    Node *rn = node_page->GetAddress<Node>();

    fault_injection(rn);

    node_page->SetDirty();
  }

  ASSERT_EQ(fs.GetVCache().RemoveDirty(test_vnode.get()), ZX_OK);
  fs.EvictVnode(test_vnode.get());

  // test vget
  ASSERT_EQ(VnodeF2fs::Vget(&fs, nid, &test_vnode), expected_status);

  test_vnode = nullptr;
}

TEST_F(VnodeTest, Time) {
  fbl::RefPtr<fs::Vnode> test_fs_vnode;
  std::string dir_name("test");
  ASSERT_EQ(root_dir_->Create(dir_name, S_IFDIR, &test_fs_vnode), ZX_OK);
  fbl::RefPtr<VnodeF2fs> test_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(test_fs_vnode));

  ASSERT_EQ(test_vnode->GetNameView(), dir_name);

  timespec cur_time;
  clock_gettime(CLOCK_REALTIME, &cur_time);
  ASSERT_LE(zx::duration(test_vnode->GetATime()), zx::duration(cur_time));
  ASSERT_LE(zx::duration(test_vnode->GetMTime()), zx::duration(cur_time));
  ASSERT_LE(zx::duration(test_vnode->GetCTime()), zx::duration(cur_time));

  ASSERT_EQ(test_vnode->Close(), ZX_OK);
  test_vnode = nullptr;
}

TEST_F(VnodeTest, Advise) {
  fbl::RefPtr<fs::Vnode> test_fs_vnode;
  std::string dir_name("test");
  ASSERT_EQ(root_dir_->Create(dir_name, S_IFDIR, &test_fs_vnode), ZX_OK);
  fbl::RefPtr<VnodeF2fs> test_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(test_fs_vnode));
  Dir *test_dir_ptr = static_cast<Dir *>(test_vnode.get());

  ASSERT_EQ(test_vnode->GetNameView(), dir_name);

  FileTester::CreateChild(test_dir_ptr, S_IFDIR, "f2fs_lower_case.avi");
  fbl::RefPtr<fs::Vnode> file_fs_vnode;
  FileTester::Lookup(test_dir_ptr, "f2fs_lower_case.avi", &file_fs_vnode);
  fbl::RefPtr<VnodeF2fs> file_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(file_fs_vnode));

  uint8_t i_advise = file_vnode->GetAdvise();
  ASSERT_FALSE(TestBit(static_cast<int>(FAdvise::kCold), &i_advise));
  ASSERT_EQ(file_vnode->Close(), ZX_OK);

  FileTester::CreateChild(test_dir_ptr, S_IFDIR, "f2fs_upper_case.AVI");
  FileTester::Lookup(test_dir_ptr, "f2fs_upper_case.AVI", &file_fs_vnode);
  file_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(file_fs_vnode));

  i_advise = file_vnode->GetAdvise();
  ASSERT_FALSE(TestBit(static_cast<int>(FAdvise::kCold), &i_advise));

  test_dir_ptr->SetColdFile(*file_vnode);
  i_advise = file_vnode->GetAdvise();
  ASSERT_TRUE(TestBit(static_cast<int>(FAdvise::kCold), &i_advise));

  file_vnode->ClearAdvise(FAdvise::kCold);
  i_advise = file_vnode->GetAdvise();
  ASSERT_FALSE(TestBit(static_cast<int>(FAdvise::kCold), &i_advise));

  ASSERT_EQ(file_vnode->Close(), ZX_OK);
  file_vnode = nullptr;
  ASSERT_EQ(test_vnode->Close(), ZX_OK);
  test_vnode = nullptr;
}

TEST_F(VnodeTest, EmptyOverridenMethods) {
  char buf[kPageSize];
  size_t out, end;
  zx::vmo vmo;
  ASSERT_EQ(root_dir_->Read(buf, 0, kPageSize, &out), ZX_ERR_NOT_SUPPORTED);
  ASSERT_EQ(root_dir_->Write(buf, 0, kPageSize, &out), ZX_ERR_NOT_SUPPORTED);
  ASSERT_EQ(root_dir_->Append(buf, kPageSize, &end, &out), ZX_ERR_NOT_SUPPORTED);
  ASSERT_EQ(root_dir_->Truncate(0), ZX_ERR_NOT_SUPPORTED);
  ASSERT_EQ(root_dir_->PopulateVmoWithInlineData(vmo).status_value(), ZX_ERR_NOT_SUPPORTED);
}

TEST_F(VnodeTest, Mode) {
  fbl::RefPtr<fs::Vnode> dir_fs_vnode;
  std::string dir_name("test_dir");
  ASSERT_EQ(root_dir_->Create(dir_name, S_IFDIR, &dir_fs_vnode), ZX_OK);
  fbl::RefPtr<VnodeF2fs> dir_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(dir_fs_vnode));

  ASSERT_TRUE(S_ISDIR(dir_vnode->GetMode()));
  ASSERT_EQ(dir_vnode->IsDir(), true);
  ASSERT_EQ(dir_vnode->IsReg(), false);
  ASSERT_EQ(dir_vnode->IsLink(), false);
  ASSERT_EQ(dir_vnode->IsChr(), false);
  ASSERT_EQ(dir_vnode->IsBlk(), false);
  ASSERT_EQ(dir_vnode->IsSock(), false);
  ASSERT_EQ(dir_vnode->IsFifo(), false);

  ASSERT_EQ(dir_vnode->Close(), ZX_OK);
  dir_vnode = nullptr;

  fbl::RefPtr<fs::Vnode> file_fs_vnode;
  std::string file_name("test_file");
  ASSERT_EQ(root_dir_->Create(file_name, S_IFREG, &file_fs_vnode), ZX_OK);
  fbl::RefPtr<VnodeF2fs> file_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(file_fs_vnode));

  ASSERT_TRUE(S_ISREG(file_vnode->GetMode()));
  ASSERT_EQ(file_vnode->IsDir(), false);
  ASSERT_EQ(file_vnode->IsReg(), true);
  ASSERT_EQ(file_vnode->IsLink(), false);
  ASSERT_EQ(file_vnode->IsChr(), false);
  ASSERT_EQ(file_vnode->IsBlk(), false);
  ASSERT_EQ(file_vnode->IsSock(), false);
  ASSERT_EQ(file_vnode->IsFifo(), false);

  ASSERT_EQ(file_vnode->Close(), ZX_OK);
  file_vnode = nullptr;
}

TEST_F(VnodeTest, WriteInode) {
  fbl::RefPtr<VnodeF2fs> test_vnode;
  NodeManager &node_manager = fs_->GetNodeManager();

  // 1. Check node ino exception
  ASSERT_EQ(VnodeF2fs::Vget(fs_.get(), fs_->GetSuperblockInfo().GetNodeIno(), &test_vnode), ZX_OK);
  ASSERT_EQ(test_vnode->WriteInode(false), ZX_OK);
  fs_->EvictVnode(test_vnode.get());
  test_vnode = nullptr;

  // 2. Check GetNodePage() exception
  FileTester::CreateChild(root_dir_.get(), S_IFDIR, "write_inode_dir");
  fbl::RefPtr<fs::Vnode> dir_raw_vnode;
  FileTester::Lookup(root_dir_.get(), "write_inode_dir", &dir_raw_vnode);
  test_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(dir_raw_vnode));
  nid_t nid = test_vnode->GetKey();

  ASSERT_EQ(test_vnode->WriteInode(false), ZX_OK);

  block_t temp_block_address;
  MapTester::GetCachedNatEntryBlockAddress(node_manager, nid, temp_block_address);

  // Enable fault injection to dnode(vnode)
  MapTester::SetCachedNatEntryBlockAddress(node_manager, nid, kNullAddr);
  ASSERT_EQ(test_vnode->WriteInode(false), ZX_ERR_NOT_FOUND);

  // Disable fault injection
  MapTester::SetCachedNatEntryBlockAddress(node_manager, nid, temp_block_address);
  ASSERT_EQ(test_vnode->WriteInode(false), ZX_OK);

  // 3. Is clean inode
  ASSERT_TRUE(test_vnode->IsDirty());
  fs_->WriteCheckpoint(false, false);
  ASSERT_FALSE(test_vnode->IsDirty());

  ASSERT_EQ(test_vnode->Close(), ZX_OK);
  test_vnode = nullptr;
}

TEST_F(VnodeTest, VgetExceptionCase) {
  fbl::RefPtr<VnodeF2fs> test_vnode;
  NodeManager &node_manager = fs_->GetNodeManager();
  nid_t nid;

  // 1. Check Create() GetNodePage() exception
  node_manager.AllocNid(nid);
  node_manager.AllocNidDone(nid);
  ASSERT_EQ(VnodeF2fs::Vget(fs_.get(), nid, &test_vnode), ZX_ERR_NOT_FOUND);

  // 2. Check Create() namelen exception
  auto namelen_fault_inject = [](Node *rn) { rn->i.i_namelen = 0; };
  VgetFaultInjetionAndTest(*fs_, *root_dir_, "namelen_dir", namelen_fault_inject, ZX_ERR_NOT_FOUND);

  // 3. Check Vget() GetNlink() exception
  auto nlink_fault_inject = [](Node *rn) { rn->i.i_links = 0; };
  VgetFaultInjetionAndTest(*fs_, *root_dir_, "nlink_dir", nlink_fault_inject, ZX_ERR_NOT_FOUND);

  test_vnode = nullptr;
}

TEST_F(VnodeTest, SetAttributes) {
  fbl::RefPtr<fs::Vnode> dir_fs_vnode;
  std::string dir_name("test_dir");
  ASSERT_EQ(root_dir_->Create(dir_name, S_IFDIR, &dir_fs_vnode), ZX_OK);
  fbl::RefPtr<VnodeF2fs> dir_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(dir_fs_vnode));

  ASSERT_EQ(dir_vnode->SetAttributes(fs::VnodeAttributesUpdate()
                                         .set_modification_time(std::nullopt)
                                         .set_creation_time(std::nullopt)),
            ZX_OK);
  ASSERT_EQ(dir_vnode->SetAttributes(fs::VnodeAttributesUpdate()
                                         .set_modification_time(std::make_optional(1UL))
                                         .set_creation_time(std::make_optional(1UL))),
            ZX_OK);

  ASSERT_EQ(dir_vnode->Close(), ZX_OK);
  dir_vnode = nullptr;
}

TEST_F(VnodeTest, TruncateExceptionCase) {
  fbl::RefPtr<fs::Vnode> file_fs_vnode;
  std::string file_name("test_file");
  ASSERT_EQ(root_dir_->Create(file_name, S_IFREG, &file_fs_vnode), ZX_OK);
  fbl::RefPtr<VnodeF2fs> file_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(file_fs_vnode));

  // 1. Check TruncatePartialDataPage() exception
  file_vnode->SetSize(1);
  file_vnode->TruncatePartialDataPage(1);
  ASSERT_EQ(file_vnode->GetSize(), 1UL);

  // 2. Check TruncateBlocks() exception
  file_vnode->SetSize(1);
  ASSERT_EQ(file_vnode->TruncateBlocks(1), ZX_OK);
  ASSERT_EQ(file_vnode->GetSize(), 1UL);

  const pgoff_t direct_index = 1;
  const pgoff_t direct_blks = kAddrsPerBlock;
  const pgoff_t indirect_blks = static_cast<const pgoff_t>(kAddrsPerBlock) * kNidsPerBlock;
  const pgoff_t indirect_index_lv1 = direct_index + kAddrsPerInode;
  const pgoff_t indirect_index_lv2 = indirect_index_lv1 + direct_blks * 2;
  const pgoff_t indirect_index_lv3 = indirect_index_lv2 + indirect_blks * 2;
  pgoff_t indirect_index_invalid_lv4 = indirect_index_lv3 + indirect_blks * kNidsPerBlock;
  uint32_t blocksize = fs_->GetSuperblockInfo().GetBlocksize();
  uint64_t invalid_size = indirect_index_invalid_lv4 * blocksize;

  file_vnode->SetSize(invalid_size);
  ASSERT_EQ(file_vnode->TruncateBlocks(invalid_size), ZX_ERR_NOT_FOUND);
  ASSERT_EQ(file_vnode->GetSize(), invalid_size);

  // 3. Check TruncateHole() exception
  file_vnode->SetSize(invalid_size);
  ASSERT_EQ(file_vnode->TruncateHole(invalid_size, invalid_size + 1), ZX_OK);
  ASSERT_EQ(file_vnode->GetSize(), invalid_size);

  ASSERT_EQ(file_vnode->Close(), ZX_OK);
  file_vnode = nullptr;

  // 4. Check TruncateToSize() exception
  fbl::RefPtr<fs::Vnode> block_fs_vnode;
  std::string block_name("test_block");
  ASSERT_EQ(root_dir_->Create(block_name, S_IFBLK, &block_fs_vnode), ZX_OK);
  fbl::RefPtr<VnodeF2fs> block_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(block_fs_vnode));
  uint64_t block_size = block_vnode->GetSize();
  block_vnode->TruncateToSize();
  ASSERT_EQ(block_vnode->GetSize(), block_size);

  ASSERT_EQ(block_vnode->Close(), ZX_OK);
  block_vnode = nullptr;
}

TEST_F(VnodeTest, SyncFile) {
  fbl::RefPtr<fs::Vnode> file_fs_vnode;
  std::string file_name("test_dir");
  ASSERT_EQ(root_dir_->Create(file_name, S_IFREG, &file_fs_vnode), ZX_OK);
  fbl::RefPtr<VnodeF2fs> file_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(file_fs_vnode));

  // 1. Check need_cp
  uint64_t pre_checkpoint_ver = fs_->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  fs_->GetSuperblockInfo().ClearOpt(kMountDisableRollForward);
  file_vnode->SetFlag(InodeInfoFlag::kDirty);
  ASSERT_EQ(file_vnode->SyncFile(0, safemath::checked_cast<loff_t>(file_vnode->GetSize()), 0),
            ZX_OK);
  uint64_t curr_checkpoint_ver = fs_->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  ASSERT_EQ(pre_checkpoint_ver, curr_checkpoint_ver);
  fs_->GetSuperblockInfo().SetOpt(kMountDisableRollForward);

  // 2. Check vnode is clean
  pre_checkpoint_ver = fs_->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  file_vnode->ClearDirty();
  ASSERT_EQ(file_vnode->SyncFile(0, safemath::checked_cast<loff_t>(file_vnode->GetSize()), 0),
            ZX_OK);
  curr_checkpoint_ver = fs_->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  ASSERT_EQ(pre_checkpoint_ver, curr_checkpoint_ver);

  // 3. Check kNeedCp
  pre_checkpoint_ver = fs_->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  file_vnode->SetFlag(InodeInfoFlag::kNeedCp);
  file_vnode->SetFlag(InodeInfoFlag::kDirty);
  ASSERT_EQ(file_vnode->SyncFile(0, safemath::checked_cast<loff_t>(file_vnode->GetSize()), 0),
            ZX_OK);
  ASSERT_FALSE(file_vnode->TestFlag(InodeInfoFlag::kNeedCp));
  curr_checkpoint_ver = fs_->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  ASSERT_EQ(pre_checkpoint_ver + 1, curr_checkpoint_ver);

  // 4. Check SpaceForRollForward()
  pre_checkpoint_ver = fs_->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  block_t temp_user_block_count = fs_->GetSuperblockInfo().GetUserBlockCount();
  fs_->GetSuperblockInfo().SetUserBlockCount(0);
  file_vnode->SetFlag(InodeInfoFlag::kDirty);
  ASSERT_EQ(file_vnode->SyncFile(0, safemath::checked_cast<loff_t>(file_vnode->GetSize()), 0),
            ZX_OK);
  ASSERT_FALSE(file_vnode->TestFlag(InodeInfoFlag::kNeedCp));
  curr_checkpoint_ver = fs_->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  ASSERT_EQ(pre_checkpoint_ver + 1, curr_checkpoint_ver);
  fs_->GetSuperblockInfo().SetUserBlockCount(temp_user_block_count);

  ASSERT_EQ(file_vnode->Close(), ZX_OK);
  file_vnode = nullptr;
}

TEST_F(VnodeTest, GrabCachePages) {
  fbl::RefPtr<fs::Vnode> file_fs_vnode;
  std::string file_name("test_file");
  ASSERT_EQ(root_dir_->Create(file_name, S_IFDIR, &file_fs_vnode), ZX_OK);
  fbl::RefPtr<VnodeF2fs> file_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(file_fs_vnode));

  constexpr pgoff_t kStartOffset = 0;
  constexpr pgoff_t kEndOffset = 1000;

  {
    auto pages_or = file_vnode->GrabCachePages(kStartOffset, kEndOffset);
    ASSERT_TRUE(pages_or.is_ok());
    for (pgoff_t i = kStartOffset; i < kEndOffset; ++i) {
      LockedPage locked_page = std::move(pages_or.value()[i]);
      auto unlocked_page = locked_page.release();
      ASSERT_EQ(file_vnode->GrabCachePage(i, &locked_page), ZX_OK);
      ASSERT_EQ(locked_page.get(), unlocked_page.get());
    }
  }

  // Test with holes
  {
    std::vector<pgoff_t> pg_offsets(kEndOffset - kStartOffset);
    std::iota(pg_offsets.begin(), pg_offsets.end(), kStartOffset);
    for (size_t i = 0; i < pg_offsets.size(); i += 2) {
      pg_offsets[i] = kInvalidPageOffset;
    }
    auto pages_or = file_vnode->GrabCachePages(pg_offsets);
    ASSERT_TRUE(pages_or.is_ok());
    for (size_t i = 0; i < pg_offsets.size(); ++i) {
      if (pg_offsets[i] == kInvalidPageOffset) {
        ASSERT_FALSE(pages_or.value()[i]);
      } else {
        LockedPage locked_page = std::move(pages_or.value()[i]);
        auto unlocked_page = locked_page.release();
        ASSERT_EQ(file_vnode->GrabCachePage(pg_offsets[i], &locked_page), ZX_OK);
        ASSERT_EQ(locked_page.get(), unlocked_page.get());
      }
    }
  }

  ASSERT_EQ(file_vnode->Close(), ZX_OK);
  file_vnode = nullptr;
}

void CheckDataPages(LockedPagesAndAddrs &address_and_pages, pgoff_t start_offset,
                    pgoff_t end_offset, std::set<block_t> removed_pages) {
  for (uint32_t offset = 0; offset < end_offset - start_offset; ++offset) {
    if (removed_pages.find(offset) != removed_pages.end()) {
      ASSERT_EQ(address_and_pages.block_addrs[offset], kNullAddr);
      continue;
    }
    ASSERT_EQ(*address_and_pages.pages[offset]->GetAddress<uint32_t>(),
              static_cast<uint32_t>(offset));
  }
}

TEST_F(VnodeTest, FindDataBlockAddrsAndPages) {
  fbl::RefPtr<fs::Vnode> file_fs_vnode;
  std::string file_name("test_file");
  ASSERT_EQ(root_dir_->Create(file_name, S_IFREG, &file_fs_vnode), ZX_OK);
  fbl::RefPtr<File> file = fbl::RefPtr<File>::Downcast(std::move(file_fs_vnode));

  constexpr pgoff_t kStartOffset = 0;
  constexpr pgoff_t kEndOffset = 1000;
  constexpr pgoff_t kMidOffset = kEndOffset / 2;
  constexpr pgoff_t kPageCount = kEndOffset - kStartOffset;
  uint32_t page_count = kPageCount;
  std::set<block_t> removed_pages;

  // Get null block address
  {
    auto addrs_and_pages_or = file->FindDataBlockAddrsAndPages(kStartOffset, kEndOffset);
    ASSERT_TRUE(addrs_and_pages_or.is_ok());
    ASSERT_EQ(addrs_and_pages_or->block_addrs.size(), page_count);
    ASSERT_EQ(addrs_and_pages_or->pages.size(), page_count);
  }

  // Get valid block address
  {
    uint32_t buf[kPageSize / sizeof(uint32_t)];
    for (uint32_t i = 0; i < static_cast<uint32_t>(kPageCount); ++i) {
      buf[0] = i;
      FileTester::AppendToFile(file.get(), buf, kPageSize);
    }
    file->SyncFile(0, safemath::checked_cast<loff_t>(file->GetSize()), 0);

    auto addrs_and_pages_or = file->FindDataBlockAddrsAndPages(kStartOffset, kEndOffset);
    ASSERT_TRUE(addrs_and_pages_or.is_ok());
    ASSERT_EQ(addrs_and_pages_or->block_addrs.size(), page_count);
    ASSERT_EQ(addrs_and_pages_or->pages.size(), page_count);
    CheckDataPages(addrs_and_pages_or.value(), kStartOffset, kEndOffset, removed_pages);
  }

  // Punch a hole at start
  {
    file->TruncateHole(kStartOffset, kStartOffset + 1);
    removed_pages.insert(kStartOffset);

    auto addrs_and_pages_or = file->FindDataBlockAddrsAndPages(kStartOffset, kEndOffset);
    ASSERT_TRUE(addrs_and_pages_or.is_ok());
    ASSERT_EQ(addrs_and_pages_or->block_addrs.size(), page_count);
    ASSERT_EQ(addrs_and_pages_or->pages.size(), page_count);
    CheckDataPages(addrs_and_pages_or.value(), kStartOffset, kEndOffset, removed_pages);
  }

  // Punch a hole at end
  {
    file->TruncateHole(kEndOffset - 1, kEndOffset);
    removed_pages.insert(kEndOffset - 1);

    auto addrs_and_pages_or = file->FindDataBlockAddrsAndPages(kStartOffset, kEndOffset);
    ASSERT_TRUE(addrs_and_pages_or.is_ok());
    ASSERT_EQ(addrs_and_pages_or->block_addrs.size(), page_count);
    ASSERT_EQ(addrs_and_pages_or->pages.size(), page_count);
    CheckDataPages(addrs_and_pages_or.value(), kStartOffset, kEndOffset, removed_pages);
  }

  // Punch holes at middle
  {
    constexpr uint32_t kPunchHoles = 10;
    file->TruncateHole(kMidOffset, kMidOffset + kPunchHoles);
    for (uint32_t i = 0; i < kPunchHoles; ++i) {
      removed_pages.insert(kMidOffset + i);
    }

    auto addrs_and_pages_or = file->FindDataBlockAddrsAndPages(kStartOffset, kEndOffset);
    ASSERT_TRUE(addrs_and_pages_or.is_ok());
    ASSERT_EQ(addrs_and_pages_or->block_addrs.size(), page_count);
    ASSERT_EQ(addrs_and_pages_or->pages.size(), page_count);
    CheckDataPages(addrs_and_pages_or.value(), kStartOffset, kEndOffset, removed_pages);
  }

  ASSERT_EQ(file->Close(), ZX_OK);
  file = nullptr;
}

}  // namespace
}  // namespace f2fs
