// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "unit_lib.h"

namespace f2fs {
namespace {

TEST(FsckTest, InvalidSuperblockMagic) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);
  FsckWorker fsck(std::move(bc), FsckOptions{.repair = false});

  ASSERT_EQ(fsck.GetValidSuperblock(), ZX_OK);

  // Get the first superblock.
  auto ret = fsck.GetSuperblock(0);
  ASSERT_TRUE(ret.is_ok());
  std::unique_ptr<FsBlock> superblock = std::move(*ret);

  auto superblock_pointer =
      reinterpret_cast<Superblock *>(superblock->GetData().data() + kSuperOffset);
  ASSERT_EQ(fsck.SanityCheckRawSuper(superblock_pointer), ZX_OK);

  // Pollute the first superblock and see validation fails.
  superblock_pointer->magic = 0xdeadbeef;
  ASSERT_EQ(fsck.SanityCheckRawSuper(superblock_pointer), ZX_ERR_INTERNAL);
  ASSERT_EQ(fsck.WriteBlock(*superblock.get(), kSuperblockStart), ZX_OK);

  // Superblock load does not fail yet, since f2fs keeps a spare superblock.
  ASSERT_EQ(fsck.GetValidSuperblock(), ZX_OK);

  // Pollute the second superblock, fsck won't proceed.
  ASSERT_EQ(fsck.WriteBlock(*superblock.get(), kSuperblockStart + 1), ZX_OK);
  ASSERT_EQ(fsck.GetValidSuperblock(), ZX_ERR_NOT_FOUND);
  ASSERT_EQ(fsck.Run(), ZX_ERR_NOT_FOUND);
}

TEST(FsckTest, InvalidCheckpointCrc) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);
  FsckWorker fsck(std::move(bc), FsckOptions{.repair = false});

  ASSERT_EQ(fsck.GetValidSuperblock(), ZX_OK);
  ASSERT_EQ(fsck.GetValidCheckpoint(), ZX_OK);

  auto ret = fsck.GetSuperblock(0);
  ASSERT_TRUE(ret.is_ok());
  Superblock *superblock_pointer =
      reinterpret_cast<Superblock *>(ret->GetData().data() + kSuperOffset);

  // Read the 1st checkpoint pack header.
  uint32_t first_checkpoint_header_addr = LeToCpu(superblock_pointer->cp_blkaddr);
  ASSERT_TRUE(fsck.ValidateCheckpoint(first_checkpoint_header_addr).is_ok());
  auto first_checkpoint_block = std::make_unique<FsBlock>();
  ASSERT_EQ(fsck.ReadBlock(*first_checkpoint_block.get(), first_checkpoint_header_addr), ZX_OK);

  // Pollute the 1st checkpoint pack header and see validation fails.
  auto checkpoint_ptr = reinterpret_cast<Checkpoint *>(first_checkpoint_block.get());
  auto elapsed_time_saved = checkpoint_ptr->elapsed_time;
  checkpoint_ptr->elapsed_time = 0xdeadbeef;
  ASSERT_EQ(fsck.WriteBlock(*first_checkpoint_block.get(), first_checkpoint_header_addr), ZX_OK);
  ASSERT_FALSE(fsck.ValidateCheckpoint(first_checkpoint_header_addr).is_ok());

  // Checkpoint load does not fail, since f2fs keeps 2 checkpoint packs.
  ASSERT_EQ(fsck.GetValidCheckpoint(), ZX_OK);

  // Read the 2nd checkpoint header.
  uint32_t second_checkpoint_header_addr = LeToCpu(superblock_pointer->cp_blkaddr) +
                                           (1 << LeToCpu(superblock_pointer->log_blocks_per_seg));
  ASSERT_TRUE(fsck.ValidateCheckpoint(second_checkpoint_header_addr).is_ok());
  auto second_checkpoint_block = std::make_unique<FsBlock>();
  ASSERT_EQ(fsck.ReadBlock(*second_checkpoint_block.get(), second_checkpoint_header_addr), ZX_OK);

  // This time pollute the checkpoint pack footer and see validation fails.
  uint32_t second_checkpoint_footer_addr =
      second_checkpoint_header_addr +
      LeToCpu((reinterpret_cast<Checkpoint *>(second_checkpoint_block.get()))
                  ->cp_pack_total_block_count) -
      1;
  ASSERT_EQ(fsck.ReadBlock(*second_checkpoint_block.get(), second_checkpoint_footer_addr), ZX_OK);
  checkpoint_ptr = reinterpret_cast<Checkpoint *>(second_checkpoint_block.get());
  checkpoint_ptr->next_free_nid = 0xdeadbeef;
  ASSERT_EQ(fsck.WriteBlock(*second_checkpoint_block.get(), second_checkpoint_footer_addr), ZX_OK);
  ASSERT_FALSE(fsck.ValidateCheckpoint(second_checkpoint_header_addr).is_ok());

  // Both checkpoint packs are polluted, checkpoint load fails.
  ASSERT_EQ(fsck.GetValidCheckpoint(), ZX_ERR_NOT_FOUND);
  ASSERT_EQ(fsck.Run(), ZX_ERR_NOT_FOUND);

  // This time roll back the 1st checkpoint header, leaving 2nd one polluted.
  checkpoint_ptr = reinterpret_cast<Checkpoint *>(first_checkpoint_block.get());
  checkpoint_ptr->elapsed_time = elapsed_time_saved;
  ASSERT_EQ(fsck.WriteBlock(*first_checkpoint_block.get(), first_checkpoint_header_addr), ZX_OK);
  ASSERT_EQ(fsck.GetValidCheckpoint(), ZX_OK);
  ASSERT_EQ(fsck.Run(), ZX_OK);
}

TEST(FsckTest, UnreachableNatEntry) {
  constexpr uint32_t fake_nid = 13u;
  constexpr uint32_t fake_ino = 7u;
  constexpr uint32_t fake_block_addr = 123u;

  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);
  FsckWorker fsck(std::move(bc), FsckOptions{.repair = false});

  // Read the superblock to locate NAT.
  auto ret = fsck.GetSuperblock(0);
  ASSERT_TRUE(ret.is_ok());
  auto superblock_pointer = reinterpret_cast<Superblock *>(ret->GetData().data() + kSuperOffset);

  // Read the NAT block.
  auto fs_block = std::make_unique<FsBlock>();
  ASSERT_EQ(fsck.ReadBlock(*fs_block.get(), LeToCpu(superblock_pointer->nat_blkaddr)), ZX_OK);

  // Insert an unreachable entry.
  auto nat_block = reinterpret_cast<NatBlock *>(fs_block->GetData().data());

  ASSERT_EQ(LeToCpu(nat_block->entries[fake_nid].ino), 0u);
  ASSERT_EQ(LeToCpu(nat_block->entries[fake_nid].block_addr), 0u);
  nat_block->entries[fake_nid] = {.ino = CpuToLe(fake_ino), .block_addr = CpuToLe(fake_block_addr)};
  ASSERT_EQ(fsck.WriteBlock(*fs_block.get(), LeToCpu(superblock_pointer->nat_blkaddr)), ZX_OK);

  // Check that the entry is correctly injected.
  ASSERT_EQ(fsck.DoMount(), ZX_OK);
  auto node_info = fsck.GetNodeInfo(fake_nid);
  ASSERT_TRUE(node_info.is_ok());
  ASSERT_EQ(LeToCpu(node_info->nid), fake_nid);
  ASSERT_EQ(LeToCpu(node_info->ino), fake_ino);
  ASSERT_EQ(LeToCpu(node_info->blk_addr), fake_block_addr);

  // Fsck should fail at verifying stage.
  ASSERT_EQ(fsck.DoFsck(), ZX_ERR_INTERNAL);

  // Try repairing the NAT.
  ASSERT_EQ(fsck.RepairNat(), ZX_OK);

  // Re-read the nat to check it is repaired.
  ASSERT_EQ(fsck.ReadBlock(*fs_block.get(), LeToCpu(superblock_pointer->nat_blkaddr)), ZX_OK);
  nat_block = reinterpret_cast<NatBlock *>(fs_block->GetData().data());
  ASSERT_EQ(LeToCpu(nat_block->entries[fake_nid].ino), 0u);
  ASSERT_EQ(LeToCpu(nat_block->entries[fake_nid].block_addr), 0u);

  // Re-insert the unreachable entry.
  nat_block->entries[fake_nid] = {.ino = CpuToLe(fake_ino), .block_addr = CpuToLe(fake_block_addr)};
  ASSERT_EQ(fsck.WriteBlock(*fs_block.get(), LeToCpu(superblock_pointer->nat_blkaddr)), ZX_OK);

  // Check that the repair option works.
  bc = fsck.Destroy();
  ASSERT_EQ(Fsck(std::move(bc), FsckOptions{.repair = true}, &bc), ZX_OK);
  ASSERT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}), ZX_OK);
}

TEST(FsckTest, UnreachableNatEntryInJournal) {
  constexpr uint32_t fake_nid = 13u;
  constexpr uint32_t fake_ino = 7u;
  constexpr uint32_t fake_block_addr = 123u;

  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);
  FsckWorker fsck(std::move(bc), FsckOptions{.repair = false});

  // Read the superblock to locate checkpoint.
  auto ret = fsck.GetSuperblock(0);
  ASSERT_TRUE(ret.is_ok());
  auto superblock_pointer = reinterpret_cast<Superblock *>(ret->GetData().data() + kSuperOffset);

  // Read the checkpoint to locate hot data summary (which holds Nat journal).
  auto checkpoint_block = std::make_unique<FsBlock>();
  ASSERT_EQ(fsck.ReadBlock(*checkpoint_block.get(), LeToCpu(superblock_pointer->cp_blkaddr)),
            ZX_OK);
  auto checkpoint_ptr = reinterpret_cast<Checkpoint *>(checkpoint_block.get());
  ASSERT_FALSE(checkpoint_ptr->ckpt_flags & static_cast<uint32_t>(CpFlag::kCpCompactSumFlag));
  auto summary_offset = checkpoint_ptr->cp_pack_start_sum;

  // Read the hot data summary.
  auto fs_block = std::make_unique<FsBlock>();
  ASSERT_EQ(
      fsck.ReadBlock(*fs_block.get(), LeToCpu(superblock_pointer->cp_blkaddr) + summary_offset),
      ZX_OK);
  auto hot_data_summary_ptr = reinterpret_cast<SummaryBlock *>(fs_block.get());
  ASSERT_EQ(hot_data_summary_ptr->n_nats, 0);

  // Insert an unreachable entry.
  hot_data_summary_ptr->nat_j.entries[hot_data_summary_ptr->n_nats++] = {
      .nid = CpuToLe(fake_nid),
      .ne = {.ino = CpuToLe(fake_ino), .block_addr = CpuToLe(fake_block_addr)},
  };
  ASSERT_EQ(
      fsck.WriteBlock(*fs_block.get(), LeToCpu(superblock_pointer->cp_blkaddr) + summary_offset),
      ZX_OK);

  // Check that the entry is correctly injected.
  ASSERT_EQ(fsck.DoMount(), ZX_OK);
  auto node_info = fsck.GetNodeInfo(fake_nid);
  ASSERT_TRUE(node_info.is_ok());
  ASSERT_EQ(LeToCpu(node_info->nid), fake_nid);
  ASSERT_EQ(LeToCpu(node_info->ino), fake_ino);
  ASSERT_EQ(LeToCpu(node_info->blk_addr), fake_block_addr);

  // Fsck should fail at verifying stage.
  ASSERT_EQ(fsck.DoFsck(), ZX_ERR_INTERNAL);

  // Try repairing the NAT.
  ASSERT_EQ(fsck.RepairNat(), ZX_OK);

  // Re-read the summary to check it is repaired.
  ASSERT_EQ(
      fsck.ReadBlock(*fs_block.get(), LeToCpu(superblock_pointer->cp_blkaddr) + summary_offset),
      ZX_OK);
  hot_data_summary_ptr = reinterpret_cast<SummaryBlock *>(fs_block.get());
  ASSERT_EQ(hot_data_summary_ptr->n_nats, 0);

  // Re-insert the unreachable entry.
  hot_data_summary_ptr->nat_j.entries[hot_data_summary_ptr->n_nats++] = {
      .nid = CpuToLe(fake_nid),
      .ne = {.ino = CpuToLe(fake_ino), .block_addr = CpuToLe(fake_block_addr)},
  };
  ASSERT_EQ(
      fsck.WriteBlock(*fs_block.get(), LeToCpu(superblock_pointer->cp_blkaddr) + summary_offset),
      ZX_OK);

  // Check that the repair option works.
  bc = fsck.Destroy();
  ASSERT_EQ(Fsck(std::move(bc), FsckOptions{.repair = true}, &bc), ZX_OK);
  ASSERT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}), ZX_OK);
}

TEST(FsckTest, UnreachableSitEntry) {
  constexpr uint32_t target_segment = 7u;
  constexpr uint32_t target_offset = 123u;

  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);
  FsckWorker fsck(std::move(bc), FsckOptions{.repair = false});

  // Read the superblock to locate SIT.
  auto ret = fsck.GetSuperblock(0);
  ASSERT_TRUE(ret.is_ok());
  auto superblock_pointer = reinterpret_cast<Superblock *>(ret->GetData().data() + kSuperOffset);

  // Read the SIT block.
  auto fs_block = std::make_unique<FsBlock>();
  ASSERT_EQ(fsck.ReadBlock(*fs_block.get(), LeToCpu(superblock_pointer->sit_blkaddr)), ZX_OK);

  // Insert an unreachable entry and update counter.
  // SIT is consistent itself but the entry is unreachable from the directory tree.
  auto sit_block = reinterpret_cast<SitBlock *>(fs_block->GetData().data());

  ASSERT_EQ(TestValidBitmap(target_offset, sit_block->entries[target_segment].valid_map), 0);
  SetValidBitmap(target_offset, sit_block->entries[target_segment].valid_map);

  sit_block->entries[target_segment].vblocks =
      CpuToLe(static_cast<uint16_t>(LeToCpu(sit_block->entries[target_segment].vblocks) + 1));

  ASSERT_EQ(fsck.WriteBlock(*fs_block.get(), LeToCpu(superblock_pointer->sit_blkaddr)), ZX_OK);

  // Fsck should fail at verifying stage.
  ASSERT_EQ(fsck.DoMount(), ZX_OK);
  ASSERT_EQ(fsck.DoFsck(), ZX_ERR_INTERNAL);

  // Try repairing the SIT.
  ASSERT_EQ(fsck.RepairSit(), ZX_OK);

  // Re-read the SIT block to check it is repaired.
  ASSERT_EQ(fsck.ReadBlock(*fs_block.get(), LeToCpu(superblock_pointer->sit_blkaddr)), ZX_OK);
  sit_block = reinterpret_cast<SitBlock *>(fs_block->GetData().data());
  ASSERT_EQ(TestValidBitmap(target_offset, sit_block->entries[target_segment].valid_map), 0);

  // Re-insert the unreachable entry.
  SetValidBitmap(target_offset, sit_block->entries[target_segment].valid_map);
  sit_block->entries[target_segment].vblocks =
      CpuToLe(static_cast<uint16_t>(LeToCpu(sit_block->entries[target_segment].vblocks) + 1));
  ASSERT_EQ(fsck.WriteBlock(*fs_block.get(), LeToCpu(superblock_pointer->sit_blkaddr)), ZX_OK);

  // Check that the repair option works.
  bc = fsck.Destroy();
  ASSERT_EQ(Fsck(std::move(bc), FsckOptions{.repair = true}, &bc), ZX_OK);
  ASSERT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}), ZX_OK);
}

TEST(FsckTest, UnreachableSitEntryInJournal) {
  constexpr uint32_t target_entry_index = 3u;
  constexpr uint32_t target_offset = 123u;

  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);
  FsckWorker fsck(std::move(bc), FsckOptions{.repair = false});

  // Read the superblock to locate SIT.
  auto ret = fsck.GetSuperblock(0);
  ASSERT_TRUE(ret.is_ok());
  auto superblock_pointer = reinterpret_cast<Superblock *>(ret->GetData().data() + kSuperOffset);

  // Read the checkpoint to locate cold data summary (which holds Sit journal).
  auto fs_block = std::make_unique<FsBlock>();
  ASSERT_EQ(fsck.ReadBlock(*fs_block.get(), LeToCpu(superblock_pointer->cp_blkaddr)), ZX_OK);
  auto cp_ptr = reinterpret_cast<Checkpoint *>(fs_block.get());
  ASSERT_FALSE(cp_ptr->ckpt_flags & static_cast<uint32_t>(CpFlag::kCpCompactSumFlag));
  auto offset = LeToCpu(superblock_pointer->cp_blkaddr) + LeToCpu(cp_ptr->cp_pack_start_sum) + 2;

  // Read the cold data summary.
  ASSERT_EQ(fsck.ReadBlock(*fs_block.get(), offset), ZX_OK);
  auto cold_data_summary_ptr = reinterpret_cast<SummaryBlock *>(fs_block.get());

  // Sit journal holds 6 summaries for open segments.
  // Set an address bit that is unreachable.
  SitEntry &target_sit_entry = cold_data_summary_ptr->sit_j.entries[target_entry_index].se;
  ASSERT_EQ(TestValidBitmap(target_offset, target_sit_entry.valid_map), 0);
  SetValidBitmap(target_offset, target_sit_entry.valid_map);
  target_sit_entry.vblocks = CpuToLe(static_cast<uint16_t>(LeToCpu(target_sit_entry.vblocks) + 1));

  ASSERT_EQ(fsck.WriteBlock(*fs_block.get(), offset), ZX_OK);

  // Fsck should fail at verifying stage.
  ASSERT_EQ(fsck.DoMount(), ZX_OK);
  ASSERT_EQ(fsck.DoFsck(), ZX_ERR_INTERNAL);

  // Try repairing the SIT.
  ASSERT_EQ(fsck.RepairSit(), ZX_OK);

  // Re-read the summary to check it is repaired.
  ASSERT_EQ(fsck.ReadBlock(*fs_block.get(), offset), ZX_OK);
  cold_data_summary_ptr = reinterpret_cast<SummaryBlock *>(fs_block.get());
  ASSERT_EQ(TestValidBitmap(target_offset, target_sit_entry.valid_map), 0);

  // Re-insert the unreachable entry.
  SitEntry &reinsert_sit_entry = cold_data_summary_ptr->sit_j.entries[target_entry_index].se;
  ASSERT_EQ(TestValidBitmap(target_offset, reinsert_sit_entry.valid_map), 0);
  SetValidBitmap(target_offset, reinsert_sit_entry.valid_map);
  reinsert_sit_entry.vblocks =
      CpuToLe(static_cast<uint16_t>(LeToCpu(reinsert_sit_entry.vblocks) + 1));
  ASSERT_EQ(fsck.WriteBlock(*fs_block.get(), offset), ZX_OK);

  // Check that the repair option works.
  bc = fsck.Destroy();
  ASSERT_EQ(Fsck(std::move(bc), FsckOptions{.repair = true}, &bc), ZX_OK);
  ASSERT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}), ZX_OK);
}

TEST(FsckTest, OrphanNodes) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  // Preconditioning
  {
    std::unique_ptr<F2fs> fs;
    async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
    MountOptions options;
    ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineData), 0), ZX_OK);
    FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

    fbl::RefPtr<VnodeF2fs> root;
    FileTester::CreateRoot(fs.get(), &root);
    fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

    fbl::RefPtr<fs::Vnode> vn;
    ASSERT_EQ(root_dir->Create("test", S_IFREG, &vn), ZX_OK);
    auto file = fbl::RefPtr<File>::Downcast(std::move(vn));

    char buf[kPageSize] = {
        0,
    };
    FileTester::AppendToFile(file.get(), buf, kPageSize);
    WritebackOperation op = {.bSync = true};
    fs->SyncDirtyDataPages(op);
    fs->WriteCheckpoint(false, false);

    FileTester::DeleteChild(root_dir.get(), "test", false);
    fs->WriteCheckpoint(false, false);

    ASSERT_EQ(file->Close(), ZX_OK);
    file = nullptr;
    ASSERT_EQ(root_dir->Close(), ZX_OK);
    root_dir = nullptr;
    FileTester::SuddenPowerOff(std::move(fs), &bc);
  }

  FsckWorker fsck(std::move(bc), FsckOptions{.repair = false});
  ASSERT_EQ(fsck.DoMount(), ZX_OK);
  ASSERT_EQ(fsck.DoFsck(), ZX_OK);
}

TEST(FsckTest, InvalidBlockAddress) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  FsckWorker fsck(std::move(bc), FsckOptions{.repair = false});
  ASSERT_EQ(fsck.DoMount(), ZX_OK);
  ASSERT_EQ(fsck.IsValidBlockAddress(0U), false);
  ASSERT_EQ(fsck.IsValidBlockAddress(std::numeric_limits<uint32_t>::max()), false);
}

TEST(FsckTest, InvalidNatEntry) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  block_t data_blkaddr;
  // Preconditioning
  {
    std::unique_ptr<F2fs> fs;
    async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
    MountOptions options;
    ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineData), 0), ZX_OK);
    FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

    fbl::RefPtr<VnodeF2fs> root;
    FileTester::CreateRoot(fs.get(), &root);
    fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

    // Find data blkaddr.
    {
      auto result = root_dir->FindDataBlkAddr(0);
      ASSERT_TRUE(result.is_ok());
      data_blkaddr = result.value();
    }

    std::vector<fbl::RefPtr<VnodeF2fs>> vnodes;
    std::vector<uint32_t> inos;
    // To allocate new node segment, inode_cnt must be bigger than kDefaultBlocksPerSegment.
    FileTester::CreateChildren(fs.get(), vnodes, inos, root_dir, "test", kDefaultBlocksPerSegment);

    for (auto &child_vn : vnodes) {
      child_vn->Close();
      child_vn = nullptr;
    }

    ASSERT_EQ(root_dir->Close(), ZX_OK);
    root_dir = nullptr;

    FileTester::Unmount(std::move(fs), &bc);
  }
  const ino_t kTestIno = 4;
  FsckWorker fsck(std::move(bc), FsckOptions{.repair = false});
  ASSERT_EQ(fsck.DoMount(), ZX_OK);

  // Read the NAT block.
  auto block_off = kTestIno / kNatEntryPerBlock;
  auto entry_off = kTestIno % kNatEntryPerBlock;
  auto seg_off = block_off >> fsck.GetSuperblockInfo().GetLogBlocksPerSeg();
  auto nat_blkaddr = (fsck.GetNodeManager().GetNatAddress() +
                      (seg_off << fsck.GetSuperblockInfo().GetLogBlocksPerSeg() << 1) +
                      (block_off & ((1 << fsck.GetSuperblockInfo().GetLogBlocksPerSeg()) - 1)));

  if (TestValidBitmap(block_off, fsck.GetNodeManager().GetNatBitmap())) {
    nat_blkaddr += fsck.GetSuperblockInfo().GetBlocksPerSeg();
  }

  auto fs_block = std::make_unique<FsBlock>();
  ASSERT_EQ(fsck.ReadBlock(*fs_block, nat_blkaddr), ZX_OK);
  NatBlock *nat_block = reinterpret_cast<NatBlock *>(fs_block->GetData().data());

  // Corrupt root_ino block address.
  ASSERT_EQ(LeToCpu(nat_block->entries[entry_off].ino), kTestIno);
  ASSERT_NE(LeToCpu(nat_block->entries[entry_off].block_addr), data_blkaddr);
  nat_block->entries[entry_off] = {.ino = CpuToLe(kTestIno),
                                   .block_addr = nat_block->entries[entry_off].block_addr + 1};
  ASSERT_EQ(fsck.WriteBlock(*fs_block.get(), nat_blkaddr), ZX_OK);

  // Fsck should fail at verifying stage.
  ASSERT_EQ(fsck.DoFsck(), ZX_ERR_INTERNAL);

  // Corrupt root_ino block address.
  nat_block->entries[entry_off] = {.ino = CpuToLe(kTestIno), .block_addr = CpuToLe(data_blkaddr)};
  ASSERT_EQ(fsck.WriteBlock(*fs_block.get(), nat_blkaddr), ZX_OK);

  // Fsck should fail at verifying stage.
  ASSERT_EQ(fsck.DoFsck(), ZX_ERR_INTERNAL);

  // Corrupt root_ino block address.
  nat_block->entries[entry_off] = {.ino = CpuToLe(kTestIno), .block_addr = CpuToLe(kNewAddr)};
  ASSERT_EQ(fsck.WriteBlock(*fs_block.get(), nat_blkaddr), ZX_OK);

  // Fsck should fail at verifying stage.
  ASSERT_EQ(fsck.DoFsck(), ZX_ERR_INTERNAL);
}

TEST(FsckTest, InvalidSsaEntry) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  block_t data_blkaddr;
  ino_t target_file_ino;
  // Preconditioning
  {
    std::unique_ptr<F2fs> fs;
    async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
    MountOptions options;
    ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineData), 0), ZX_OK);
    FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

    fbl::RefPtr<VnodeF2fs> root;
    FileTester::CreateRoot(fs.get(), &root);
    fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

    fbl::RefPtr<fs::Vnode> vn;
    ASSERT_EQ(root_dir->Create("test", S_IFREG, &vn), ZX_OK);
    auto file = fbl::RefPtr<File>::Downcast(std::move(vn));
    // To allocate new data segment, kBufferSize must be bigger than f2fs segment size.
    constexpr uint32_t kBufferSize = kBlockSize * (kDefaultBlocksPerSegment + 1);
    std::vector<char> buf(kBufferSize);
    FileTester::AppendToFile(file.get(), buf.data(), kBufferSize);
    WritebackOperation op = {.bSync = true};
    fs->SyncDirtyDataPages(op);

    // Find data blkaddr.
    {
      target_file_ino = file->GetKey();
      auto result = file->FindDataBlkAddr(0);
      ASSERT_TRUE(result.is_ok());
      data_blkaddr = result.value();
    }

    ASSERT_EQ(file->Close(), ZX_OK);
    file = nullptr;
    ASSERT_EQ(root_dir->Close(), ZX_OK);
    root_dir = nullptr;

    FileTester::Unmount(std::move(fs), &bc);
  }

  FsckWorker fsck(std::move(bc), FsckOptions{.repair = false});
  ASSERT_EQ(fsck.DoMount(), ZX_OK);

  // Read the SSA block.
  auto segno = fsck.GetSegmentNumber(data_blkaddr);
  auto blkoff_from_main = data_blkaddr - fsck.GetSegmentManager().GetMainAreaStartBlock();
  uint32_t offset = blkoff_from_main % (1 << fsck.GetSuperblockInfo().GetLogBlocksPerSeg());

  auto fs_block = std::make_unique<FsBlock>();
  block_t ssa_blkaddr = fsck.GetSegmentManager().GetSumBlock(segno);
  ASSERT_EQ(fsck.ReadBlock(*fs_block, ssa_blkaddr), ZX_OK);
  SummaryBlock *ssa_block = reinterpret_cast<SummaryBlock *>(fs_block->GetData().data());

  // Corrupt root_ino block address.
  ASSERT_EQ(LeToCpu(ssa_block->entries[offset].nid), target_file_ino);
  ++ssa_block->entries[offset].nid;
  ASSERT_EQ(fsck.WriteBlock(*fs_block.get(), ssa_blkaddr), ZX_OK);

  // Fsck should fail at verifying stage.
  ASSERT_EQ(fsck.DoFsck(), ZX_ERR_INTERNAL);
}

TEST(FsckTest, WrongInodeHardlinkCount) {
  std::unique_ptr<Bcache> bc;
  nid_t ino;
  uint32_t links;
  FileTester::MkfsOnFakeDev(&bc);

  {
    std::unique_ptr<F2fs> fs;
    async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
    FileTester::MountWithOptions(loop.dispatcher(), MountOptions{}, &bc, &fs);

    fbl::RefPtr<VnodeF2fs> root;
    FileTester::CreateRoot(fs.get(), &root);
    fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

    std::string file_name("file");
    fbl::RefPtr<fs::Vnode> child;
    ASSERT_EQ(root_dir->Create(file_name, S_IFREG, &child), ZX_OK);

    fbl::RefPtr<VnodeF2fs> child_file = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(child));

    ASSERT_EQ(root_dir->Link(std::string("link"), child_file), ZX_OK);
    ASSERT_EQ(root_dir->Link(std::string("link2"), child_file), ZX_OK);

    // Save the inode number for fsck to retrieve it.
    ino = child_file->GetKey();
    links = child_file->GetNlink();
    ASSERT_EQ(links, 3u);

    ASSERT_EQ(child_file->Close(), ZX_OK);
    child_file = nullptr;
    ASSERT_EQ(root_dir->Close(), ZX_OK);
    root_dir = nullptr;

    FileTester::Unmount(std::move(fs), &bc);
  }

  FsckWorker fsck(std::move(bc), FsckOptions{.repair = false});
  ASSERT_EQ(fsck.DoMount(), ZX_OK);

  // Retrieve the node block with the saved ino.
  auto ret = fsck.ReadNodeBlock(ino);
  ASSERT_TRUE(ret.is_ok());

  auto [fs_block, node_info] = std::move(*ret);
  auto node_block = reinterpret_cast<Node *>(fs_block->GetData().data());

  // This inode has link count 3.
  ASSERT_EQ(LeToCpu(node_block->i.i_links), links);

  // Inject fault at link count and see fsck detects it.
  node_block->i.i_links = 1u;
  ASSERT_EQ(fsck.WriteBlock(*fs_block.get(), node_info.blk_addr), ZX_OK);
  ASSERT_EQ(fsck.DoFsck(), ZX_ERR_INTERNAL);

  // Repair the link count and fsck should succeed.
  ASSERT_EQ(fsck.RepairInodeLinks(), ZX_OK);
  ASSERT_EQ(fsck.DoFsck(), ZX_OK);

  // Repeat above for some other values.
  node_block->i.i_links = 2u;
  ASSERT_EQ(fsck.WriteBlock(*fs_block.get(), node_info.blk_addr), ZX_OK);
  ASSERT_EQ(fsck.DoFsck(), ZX_ERR_INTERNAL);
  ASSERT_EQ(fsck.RepairInodeLinks(), ZX_OK);
  ASSERT_EQ(fsck.DoFsck(), ZX_OK);

  node_block->i.i_links = links + 1;
  ASSERT_EQ(fsck.WriteBlock(*fs_block.get(), node_info.blk_addr), ZX_OK);
  ASSERT_EQ(fsck.DoFsck(), ZX_ERR_INTERNAL);
  ASSERT_EQ(fsck.RepairInodeLinks(), ZX_OK);
  ASSERT_EQ(fsck.DoFsck(), ZX_OK);

  node_block->i.i_links = 0xdeadbeef;
  ASSERT_EQ(fsck.WriteBlock(*fs_block.get(), node_info.blk_addr), ZX_OK);
  ASSERT_EQ(fsck.DoFsck(), ZX_ERR_INTERNAL);
  ASSERT_EQ(fsck.RepairInodeLinks(), ZX_OK);
  ASSERT_EQ(fsck.DoFsck(), ZX_OK);
}

TEST(FsckTest, InconsistentCheckpointNodeCount) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);
  FsckWorker fsck(std::move(bc), FsckOptions{.repair = false});

  ASSERT_EQ(fsck.GetValidSuperblock(), ZX_OK);
  ASSERT_EQ(fsck.GetValidCheckpoint(), ZX_OK);

  auto ret = fsck.GetSuperblock(0);
  ASSERT_TRUE(ret.is_ok());
  Superblock *superblock_pointer =
      reinterpret_cast<Superblock *>(ret->GetData().data() + kSuperOffset);
  ASSERT_TRUE(fsck.ValidateCheckpoint(LeToCpu(superblock_pointer->cp_blkaddr)).is_ok());

  // Read the 1st checkpoint pack header.
  auto fs_block = std::make_unique<FsBlock>();
  ASSERT_EQ(fsck.ReadBlock(*fs_block.get(), LeToCpu(superblock_pointer->cp_blkaddr)), ZX_OK);

  // Modify the checkpoint's node count (and CRC).
  auto checkpoint_ptr = reinterpret_cast<Checkpoint *>(fs_block.get());
  ASSERT_EQ(checkpoint_ptr->valid_node_count, CpuToLe(1u));
  checkpoint_ptr->valid_node_count = CpuToLe(2u);
  uint32_t crc =
      F2fsCalCrc32(kF2fsSuperMagic, checkpoint_ptr, LeToCpu(checkpoint_ptr->checksum_offset));
  *(reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(checkpoint_ptr) +
                                 LeToCpu(checkpoint_ptr->checksum_offset))) = crc;

  // Write the 1st checkpoint pack, header and footer both.
  uint32_t cp_pack_block_count =
      LeToCpu((reinterpret_cast<Checkpoint *>(fs_block.get()))->cp_pack_total_block_count);
  ASSERT_EQ(fsck.WriteBlock(*fs_block.get(), LeToCpu(superblock_pointer->cp_blkaddr)), ZX_OK);
  ASSERT_EQ(fsck.WriteBlock(*fs_block.get(),
                            LeToCpu(superblock_pointer->cp_blkaddr) + cp_pack_block_count - 1),
            ZX_OK);

  // Fsck should fail at verifying stage.
  ASSERT_EQ(fsck.DoMount(), ZX_OK);
  ASSERT_EQ(fsck.DoFsck(), ZX_ERR_INTERNAL);

  // Try repairing the checkpoint.
  ASSERT_EQ(fsck.RepairCheckpoint(), ZX_OK);

  // Re-read the checkpoint pack header to check it is repaired.
  ASSERT_EQ(fsck.ReadBlock(*fs_block.get(), LeToCpu(superblock_pointer->cp_blkaddr)), ZX_OK);
  checkpoint_ptr = reinterpret_cast<Checkpoint *>(fs_block.get());
  ASSERT_EQ(checkpoint_ptr->valid_node_count, CpuToLe(1u));
  ASSERT_EQ(
      *(reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(checkpoint_ptr) +
                                     LeToCpu(checkpoint_ptr->checksum_offset))),
      F2fsCalCrc32(kF2fsSuperMagic, checkpoint_ptr, LeToCpu(checkpoint_ptr->checksum_offset)));

  // Re-insert the flaw.
  checkpoint_ptr->valid_node_count = CpuToLe(2u);
  crc = F2fsCalCrc32(kF2fsSuperMagic, checkpoint_ptr, LeToCpu(checkpoint_ptr->checksum_offset));
  *(reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(checkpoint_ptr) +
                                 LeToCpu(checkpoint_ptr->checksum_offset))) = crc;
  ASSERT_EQ(fsck.WriteBlock(*fs_block.get(), LeToCpu(superblock_pointer->cp_blkaddr)), ZX_OK);
  ASSERT_EQ(fsck.WriteBlock(*fs_block.get(),
                            LeToCpu(superblock_pointer->cp_blkaddr) + cp_pack_block_count - 1),
            ZX_OK);

  // Check that the repair option works.
  bc = fsck.Destroy();
  ASSERT_EQ(Fsck(std::move(bc), FsckOptions{.repair = true}, &bc), ZX_OK);
  ASSERT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}), ZX_OK);
}

TEST(FsckTest, InconsistentInodeFooter) {
  std::unique_ptr<Bcache> bc;
  nid_t ino;
  FileTester::MkfsOnFakeDev(&bc);

  {
    std::unique_ptr<F2fs> fs;
    async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
    FileTester::MountWithOptions(loop.dispatcher(), MountOptions{}, &bc, &fs);

    fbl::RefPtr<VnodeF2fs> root;
    FileTester::CreateRoot(fs.get(), &root);
    fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

    // Create a directory.
    std::string child_name("test");
    fbl::RefPtr<fs::Vnode> child;
    ASSERT_EQ(root_dir->Create(child_name, S_IFDIR, &child), ZX_OK);

    fbl::RefPtr<VnodeF2fs> child_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(child));

    // Save the inode number for fsck to retrieve it.
    ino = child_vnode->GetKey();

    ASSERT_EQ(child_vnode->Close(), ZX_OK);
    child_vnode = nullptr;
    ASSERT_EQ(root_dir->Close(), ZX_OK);
    root_dir = nullptr;

    FileTester::Unmount(std::move(fs), &bc);
  }

  FsckWorker fsck(std::move(bc), FsckOptions{.repair = false});
  ASSERT_EQ(fsck.DoMount(), ZX_OK);

  // Retrieve the node block with the saved ino.
  auto ret = fsck.ReadNodeBlock(ino);
  ASSERT_TRUE(ret.is_ok());

  auto [fs_block, node_info] = std::move(*ret);
  auto node_block = reinterpret_cast<Node *>(fs_block->GetData().data());
  ASSERT_EQ(fsck.ValidateNodeBlock(*node_block, node_info, FileType::kFtDir, NodeType::kTypeInode),
            ZX_OK);

  // Corrupt the node footer and see if fsck can detect it.
  node_block->footer.nid = 0xdeadbeef;
  EXPECT_EQ(fsck.ValidateNodeBlock(*node_block, node_info, FileType::kFtDir, NodeType::kTypeInode),
            ZX_ERR_INTERNAL);

  node_block->footer.nid = ino;
  node_block->footer.ino = 0xdeadbeef;
  EXPECT_EQ(fsck.ValidateNodeBlock(*node_block, node_info, FileType::kFtDir, NodeType::kTypeInode),
            ZX_ERR_INTERNAL);

  ASSERT_EQ(fsck.WriteBlock(*fs_block.get(), node_info.blk_addr), ZX_OK);
  ASSERT_EQ(fsck.Run(), ZX_ERR_INTERNAL);
}

TEST(FsckTest, InodeLinkCountAndBlockCount) {
  std::unique_ptr<Bcache> bc;
  nid_t ino;
  FileTester::MkfsOnFakeDev(&bc);

  {
    std::unique_ptr<F2fs> fs;
    async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
    FileTester::MountWithOptions(loop.dispatcher(), MountOptions{}, &bc, &fs);

    fbl::RefPtr<VnodeF2fs> root;
    FileTester::CreateRoot(fs.get(), &root);
    fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

    // Create a directory.
    std::string child_name("test");
    fbl::RefPtr<fs::Vnode> child;
    ASSERT_EQ(root_dir->Create(child_name, S_IFDIR, &child), ZX_OK);

    fbl::RefPtr<VnodeF2fs> child_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(child));

    // Save the inode number for fsck to retrieve it.
    ino = child_vnode->GetKey();

    ASSERT_EQ(child_vnode->Close(), ZX_OK);
    child_vnode = nullptr;
    ASSERT_EQ(root_dir->Close(), ZX_OK);
    root_dir = nullptr;

    FileTester::Unmount(std::move(fs), &bc);
  }

  FsckWorker fsck(std::move(bc), FsckOptions{.repair = false});
  ASSERT_EQ(fsck.DoMount(), ZX_OK);

  // Retrieve the node block with the saved ino.
  auto ret = fsck.ReadNodeBlock(ino);
  ASSERT_TRUE(ret.is_ok());

  auto [fs_block, node_info] = std::move(*ret);
  auto node_block = reinterpret_cast<Node *>(fs_block->GetData().data());
  ASSERT_EQ(fsck.ValidateNodeBlock(*node_block, node_info, FileType::kFtDir, NodeType::kTypeInode),
            ZX_OK);

  // Corrupt the link count and see if fsck can detect it.
  auto links = node_block->i.i_links;
  node_block->i.i_links = 0xdeadbeef;
  ASSERT_EQ(fsck.WriteBlock(*fs_block.get(), node_info.blk_addr), ZX_OK);
  EXPECT_EQ(fsck.Run(), ZX_ERR_INTERNAL);

  // Corrupt the block count and see if fsck can detect it.
  node_block->i.i_links = links;
  node_block->i.i_blocks = 0xdeadbeef;
  ASSERT_EQ(fsck.WriteBlock(*fs_block.get(), node_info.blk_addr), ZX_OK);
  EXPECT_EQ(fsck.Run(), ZX_ERR_INTERNAL);
}

TEST(FsckTest, InvalidNextOffsetInCurseg) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);
  FsckWorker fsck(std::move(bc), FsckOptions{.repair = false});

  ASSERT_EQ(fsck.GetValidSuperblock(), ZX_OK);
  ASSERT_EQ(fsck.GetValidCheckpoint(), ZX_OK);

  auto ret = fsck.GetSuperblock(0);
  ASSERT_TRUE(ret.is_ok());
  Superblock *superblock_pointer =
      reinterpret_cast<Superblock *>(ret->GetData().data() + kSuperOffset);
  ASSERT_TRUE(fsck.ValidateCheckpoint(LeToCpu(superblock_pointer->cp_blkaddr)).is_ok());

  // Read the 1st checkpoint pack header.
  auto fs_block = std::make_unique<FsBlock>();
  ASSERT_EQ(fsck.ReadBlock(*fs_block.get(), LeToCpu(superblock_pointer->cp_blkaddr)), ZX_OK);

  // Corrupt the next_blkoff for hot node curseg (and CRC).
  auto checkpoint_ptr = reinterpret_cast<Checkpoint *>(fs_block.get());
  ASSERT_EQ(checkpoint_ptr->cur_node_blkoff[0], CpuToLe(uint16_t{1}));
  checkpoint_ptr->cur_node_blkoff[0] = 0;
  uint32_t crc =
      F2fsCalCrc32(kF2fsSuperMagic, checkpoint_ptr, LeToCpu(checkpoint_ptr->checksum_offset));
  *(reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(checkpoint_ptr) +
                                 LeToCpu(checkpoint_ptr->checksum_offset))) = crc;

  // Write the 1st checkpoint pack, header and footer both.
  uint32_t cp_pack_block_count =
      LeToCpu((reinterpret_cast<Checkpoint *>(fs_block.get()))->cp_pack_total_block_count);
  ASSERT_EQ(fsck.WriteBlock(*fs_block.get(), LeToCpu(superblock_pointer->cp_blkaddr)), ZX_OK);
  ASSERT_EQ(fsck.WriteBlock(*fs_block.get(),
                            LeToCpu(superblock_pointer->cp_blkaddr) + cp_pack_block_count - 1),
            ZX_OK);

  // Fsck should fail at verifying stage, try repair.
  ASSERT_EQ(fsck.DoMount(), ZX_OK);
  ASSERT_EQ(fsck.DoFsck(), ZX_ERR_INTERNAL);
  ASSERT_EQ(fsck.RepairCheckpoint(), ZX_OK);

  // Re-read the checkpoint pack header to check it is repaired.
  ASSERT_EQ(fsck.ReadBlock(*fs_block.get(), LeToCpu(superblock_pointer->cp_blkaddr)), ZX_OK);
  checkpoint_ptr = reinterpret_cast<Checkpoint *>(fs_block.get());
  ASSERT_EQ(checkpoint_ptr->cur_node_blkoff[0], CpuToLe(uint16_t{1}));

  // Insert the flaw again, for hot data curseg.
  checkpoint_ptr->cur_data_blkoff[0] = 0;
  crc = F2fsCalCrc32(kF2fsSuperMagic, checkpoint_ptr, LeToCpu(checkpoint_ptr->checksum_offset));
  *(reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(checkpoint_ptr) +
                                 LeToCpu(checkpoint_ptr->checksum_offset))) = crc;
  ASSERT_EQ(fsck.WriteBlock(*fs_block.get(), LeToCpu(superblock_pointer->cp_blkaddr)), ZX_OK);
  ASSERT_EQ(fsck.WriteBlock(*fs_block.get(),
                            LeToCpu(superblock_pointer->cp_blkaddr) + cp_pack_block_count - 1),
            ZX_OK);

  // Fsck should fail at verifying stage, try repair.
  ASSERT_EQ(fsck.DoMount(), ZX_OK);
  ASSERT_EQ(fsck.DoFsck(), ZX_ERR_INTERNAL);
  ASSERT_EQ(fsck.RepairCheckpoint(), ZX_OK);

  // Re-read the checkpoint pack header to check it is repaired.
  ASSERT_EQ(fsck.ReadBlock(*fs_block.get(), LeToCpu(superblock_pointer->cp_blkaddr)), ZX_OK);
  checkpoint_ptr = reinterpret_cast<Checkpoint *>(fs_block.get());
  ASSERT_EQ(checkpoint_ptr->cur_data_blkoff[0], CpuToLe(uint16_t{1}));
}

TEST(FsckTest, WrongDataExistFlag) {
  std::unique_ptr<Bcache> bc;
  nid_t ino;
  FileTester::MkfsOnFakeDev(&bc);

  {
    std::unique_ptr<F2fs> fs;
    async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
    MountOptions options{};
    // Enable inline data option
    ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineData), 1), ZX_OK);
    FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

    fbl::RefPtr<VnodeF2fs> root;
    FileTester::CreateRoot(fs.get(), &root);
    fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

    std::string file_name("file");
    fbl::RefPtr<fs::Vnode> child;
    ASSERT_EQ(root_dir->Create(file_name, S_IFREG, &child), ZX_OK);

    // Write string and verify
    fbl::RefPtr<VnodeF2fs> child_file = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(child));
    File *child_file_ptr = static_cast<File *>(child_file.get());
    const std::string_view data_string = "hello";
    FileTester::AppendToFile(child_file_ptr, data_string.data(), data_string.size());
    ASSERT_EQ(child_file_ptr->GetSize(), data_string.size());

    char r_buf[data_string.size()];
    size_t out;
    ASSERT_EQ(child_file_ptr->Read(r_buf, data_string.size(), 0, &out), ZX_OK);
    ASSERT_EQ(out, data_string.size());
    ASSERT_EQ(memcmp(r_buf, data_string.data(), data_string.size()), 0);

    // Save the inode number for fsck to retrieve it
    ino = child_file->GetKey();

    ASSERT_EQ(child_file->Close(), ZX_OK);
    child_file = nullptr;
    ASSERT_EQ(root_dir->Close(), ZX_OK);
    root_dir = nullptr;

    FileTester::Unmount(std::move(fs), &bc);
  }

  FsckWorker fsck(std::move(bc), FsckOptions{.repair = false});
  ASSERT_EQ(fsck.DoMount(), ZX_OK);

  // Retrieve node block with saved ino
  auto ret = fsck.ReadNodeBlock(ino);
  ASSERT_TRUE(ret.is_ok());

  auto [fs_block, node_info] = std::move(*ret);
  auto node_block = reinterpret_cast<Node *>(fs_block->GetData().data());

  // Data exist flag should be set
  ASSERT_NE(node_block->i.i_inline & kDataExist, 0);

  // Inject fault and see fsck detects it
  node_block->i.i_inline &= ~kDataExist;
  ASSERT_EQ(fsck.WriteBlock(*fs_block.get(), node_info.blk_addr), ZX_OK);
  ASSERT_EQ(fsck.DoFsck(), ZX_ERR_INTERNAL);

  // Run fsck again with repair option
  bc = fsck.Destroy();
  FsckWorker fsck_repair(std::move(bc), FsckOptions{.repair = true});
  ASSERT_EQ(fsck_repair.Run(), ZX_OK);

  // Then check if the flag is fixed
  ASSERT_EQ(fsck_repair.DoMount(), ZX_OK);
  ret = fsck_repair.ReadNodeBlock(ino);
  ASSERT_TRUE(ret.is_ok());

  auto [fs_block_repair, node_info_repair] = std::move(*ret);
  node_block = reinterpret_cast<Node *>(fs_block_repair->GetData().data());

  ASSERT_NE(node_block->i.i_inline & kDataExist, 0);
}

TEST(FsckTest, AllocateFreeSegmapInfoAfterSPO) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  {
    std::unique_ptr<F2fs> fs;
    async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
    MountOptions options{};
    FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

    fbl::RefPtr<VnodeF2fs> root;
    FileTester::CreateRoot(fs.get(), &root);
    fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

    // Checkpoint without unmount flag
    fs->DoCheckpoint(false);

    ASSERT_EQ(root_dir->Close(), ZX_OK);
    root_dir = nullptr;

    FileTester::SuddenPowerOff(std::move(fs), &bc);
  }

  FsckWorker fsck(std::move(bc), FsckOptions{.repair = false});

  ASSERT_EQ(fsck.DoMount(), ZX_OK);

  // Check FreeSegmapInfo is valid
  ASSERT_NE(&fsck.GetSegmentManager().GetFreeSegmentInfo(), nullptr);
  ASSERT_NE(fsck.GetSegmentManager().GetFreeSegmentInfo().free_segmap, nullptr);
  ASSERT_NE(fsck.GetSegmentManager().GetFreeSegmentInfo().free_secmap, nullptr);
  ASSERT_EQ(fsck.GetSegmentManager().GetFreeSegmentInfo().free_segments, 0U);
  ASSERT_EQ(fsck.GetSegmentManager().GetFreeSegmentInfo().free_sections, 0U);

  // fsck with valid FreeSegmapInfo
  ASSERT_EQ(fsck.Run(), ZX_OK);
}

}  // namespace
}  // namespace f2fs
