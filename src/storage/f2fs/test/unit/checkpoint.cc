// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/macros.h>

#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "safemath/safe_conversions.h"
#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/storage/f2fs/f2fs.h"
#include "unit_lib.h"

namespace f2fs {
namespace {

using block_client::FakeBlockDevice;
using CheckpointTestF = F2fsFakeDevTestFixture;

constexpr uint64_t kBlockCount = 4194304;  // 2GB for SIT Bitmap TC

constexpr uint32_t kCheckpointVersionTest = 0;
constexpr uint32_t kCheckpointNatBitmapTest = 1;
constexpr uint32_t kCheckpointSitBitmapTest = 2;
constexpr uint32_t kCheckpointAddOrphanInodeTest = 3;
constexpr uint32_t kCheckpointRemoveOrphanInodeTest = 4;
constexpr uint32_t kCheckpointRecoverOrphanInodeTest = 5;
constexpr uint32_t kCheckpointCompactedSummariesTest = 6;
constexpr uint32_t kCheckpointNormalSummariesTest = 7;
constexpr uint32_t kCheckpointSitJournalTest = 8;
constexpr uint32_t kCheckpointNatJournalTest = 9;

constexpr uint32_t kCheckpointPack0 = 0;
constexpr uint32_t kCheckpointPack1 = 1;

constexpr uint32_t kCheckpointLoopCnt = 10;
constexpr uint8_t kRootDirNatBit = 0x80;
constexpr uint8_t kRootDirSitBit = 0x20;
constexpr uint32_t kMapPerSitEntry = kSitVBlockMapSize * 8;
constexpr uint32_t kOrphanInodeBlockCnt = 10;

void ReadCheckpoint(F2fs *fs, block_t cp_addr, LockedPage *cp_out) {
  LockedPage cp_page[2];  // cp_page[0]: header, cp_page[1]: footer
  uint64_t blk_size = fs->GetSuperblockInfo().GetBlocksize();
  Checkpoint *cp_block;
  uint64_t version[2];  // version[0]: header, version[1]: footer
  uint32_t crc;
  uint32_t crc_offset;

  for (int i = 0; i < 2; ++i) {
    // Read checkpoint pack header/footer
    fs->GetMetaPage(cp_addr, &cp_page[i]);
    ASSERT_NE(cp_page[i], nullptr);
    // Check header CRC
    cp_block = cp_page[i]->GetAddress<Checkpoint>();
    ASSERT_NE(cp_block, nullptr);
    crc_offset = LeToCpu(cp_block->checksum_offset);
    ASSERT_LT(crc_offset, blk_size);

    crc = *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(cp_block) + crc_offset);
    ASSERT_TRUE(F2fsCrcValid(crc, cp_block, crc_offset));

    // Get the version number
    version[i] = LeToCpu(cp_block->checkpoint_ver);

    // Read checkpoint pack footer
    cp_addr += LeToCpu(cp_block->cp_pack_total_block_count) - 1;
  }

  ASSERT_EQ(version[0], version[1]);

  *cp_out = std::move(cp_page[0]);
}

void GetLastCheckpoint(F2fs *fs, uint32_t expect_cp_position, bool after_mkfs, LockedPage *cp_out) {
  Superblock &fsb = fs->RawSb();
  Checkpoint *cp_block1 = nullptr, *cp_block2 = nullptr;
  LockedPage cp_page1, cp_page2;
  block_t cp_addr;

  cp_addr = LeToCpu(fsb.cp_blkaddr);
  ReadCheckpoint(fs, cp_addr, &cp_page1);
  cp_block1 = cp_page1->GetAddress<Checkpoint>();

  if (!after_mkfs) {
    cp_addr += 1 << LeToCpu(fsb.log_blocks_per_seg);
    ReadCheckpoint(fs, cp_addr, &cp_page2);
    cp_block2 = cp_page2->GetAddress<Checkpoint>();
  }

  if (after_mkfs) {
    *cp_out = std::move(cp_page1);
  } else if (cp_block1 && cp_block2) {
    if (VerAfter(cp_block2->checkpoint_ver, cp_block1->checkpoint_ver)) {
      *cp_out = std::move(cp_page2);
      ASSERT_EQ(cp_block1->checkpoint_ver, cp_block2->checkpoint_ver - 1);
    } else {
      *cp_out = std::move(cp_page1);
      ASSERT_EQ(cp_block2->checkpoint_ver, cp_block1->checkpoint_ver - 1);
    }
  } else {
    ASSERT_EQ(0, 1);
  }
}

inline void *GetBitmapPtr(Checkpoint *ckpt, MetaBitmap flag) {
  uint32_t offset = (flag == MetaBitmap::kNatBitmap) ? ckpt->sit_ver_bitmap_bytesize : 0;
  return &ckpt->sit_nat_version_bitmap + offset;
}

void CreateDirs(F2fs *fs, int dir_cnt, uint64_t version) {
  fbl::RefPtr<VnodeF2fs> data_root;
  ASSERT_EQ(VnodeF2fs::Vget(fs, fs->RawSb().root_ino, &data_root), ZX_OK);
  Dir *root_dir = static_cast<Dir *>(data_root.get());
  std::string filename;

  for (int i = 0; i < dir_cnt; ++i) {
    fbl::RefPtr<fs::Vnode> vnode;
    filename = "dir_" + std::to_string(version) + "_" + std::to_string(i);
    ASSERT_EQ(root_dir->Create(filename.c_str(), S_IFDIR, &vnode), ZX_OK);
    vnode->Close();
    vnode.reset();
  }
}

void CreateFiles(F2fs *fs, int file_cnt, uint64_t version) {
  fbl::RefPtr<VnodeF2fs> data_root;
  ASSERT_EQ(VnodeF2fs::Vget(fs, fs->RawSb().root_ino, &data_root), ZX_OK);
  Dir *root_dir = static_cast<Dir *>(data_root.get());
  std::string filename;

  for (int i = 0; i < file_cnt; ++i) {
    fbl::RefPtr<fs::Vnode> vnode;
    filename = "file_" + std::to_string(version) + "_" + std::to_string(i);
    ASSERT_EQ(root_dir->Create(filename.c_str(), S_IFREG, &vnode), ZX_OK);
    vnode->Close();
    vnode.reset();
  }
}

void DoWriteSit(F2fs *fs, block_t *new_blkaddr, CursegType type, uint32_t exp_segno) {
  SuperblockInfo &superblock_info = fs->GetSuperblockInfo();
  SitInfo &sit_i = fs->GetSegmentManager().GetSitInfo();

  if (!fs->GetSegmentManager().HasCursegSpace(type)) {
    fs->GetSegmentManager().AllocateSegmentByDefault(type, false);
  }

  CursegInfo *curseg = fs->GetSegmentManager().CURSEG_I(type);
  if (exp_segno != kNullSegNo)
    ASSERT_EQ(curseg->segno, exp_segno);

  std::lock_guard curseg_lock(curseg->curseg_mutex);
  *new_blkaddr = fs->GetSegmentManager().NextFreeBlkAddr(type);
  uint32_t old_cursegno = curseg->segno;

  std::lock_guard sentry_lock(sit_i.sentry_lock);
  fs->GetSegmentManager().RefreshNextBlkoff(curseg);
  superblock_info.IncBlockCount(curseg->alloc_type);

  fs->GetSegmentManager().RefreshSitEntry(kNullSegNo, *new_blkaddr);
  fs->GetSegmentManager().LocateDirtySegment(old_cursegno);
}

bool IsRootInode(CursegType curseg_type, uint32_t offset) {
  return (curseg_type == CursegType::kCursegHotData || curseg_type == CursegType::kCursegHotNode) &&
         offset == 0;
}

void CheckpointTestVersion(F2fs *fs, uint32_t expect_cp_position, uint32_t expect_cp_ver,
                           bool after_mkfs) {
  Checkpoint *cp = nullptr;
  LockedPage cp_page;

  GetLastCheckpoint(fs, expect_cp_position, after_mkfs, &cp_page);
  cp = cp_page->GetAddress<Checkpoint>();

  ASSERT_EQ(cp->checkpoint_ver, expect_cp_ver);
}

void CheckpointTestNatBitmap(F2fs *fs, uint32_t expect_cp_position, uint32_t expect_cp_ver,
                             bool after_mkfs, uint8_t *&pre_bitmap) {
  LockedPage cp_page;

  // 1. Get last checkpoint
  GetLastCheckpoint(fs, expect_cp_position, after_mkfs, &cp_page);
  Checkpoint *cp = cp_page->GetAddress<Checkpoint>();
  ASSERT_EQ(cp->checkpoint_ver, expect_cp_ver);

  // 2. Get NAT version bitmap
  uint8_t *version_bitmap = static_cast<uint8_t *>(GetBitmapPtr(cp, MetaBitmap::kNatBitmap));
  ASSERT_NE(version_bitmap, nullptr);

  if (pre_bitmap == nullptr)
    pre_bitmap = new uint8_t[cp->nat_ver_bitmap_bytesize]();

  // 3. Validate version bitmap
  // Check root dir version bitmap
  ASSERT_EQ((static_cast<uint8_t *>(version_bitmap))[0] & kRootDirNatBit,
            cp->checkpoint_ver % 2 ? 0x00 : kRootDirNatBit);

  // Check dir and file inode version bitmap
  if (!after_mkfs) {
    if (cp->checkpoint_ver % 2) {
      (static_cast<uint8_t *>(pre_bitmap))[0] &= ~kRootDirNatBit;
    } else {
      (static_cast<uint8_t *>(pre_bitmap))[0] |= kRootDirNatBit;
    }

    auto cur_nat_block = cp->checkpoint_ver - 2;
    uint8_t cur_nat_bit = 0x80 >> (cur_nat_block % 8);
    (static_cast<uint8_t *>(pre_bitmap))[cur_nat_block / 8] |= cur_nat_bit;

    ASSERT_EQ((static_cast<uint8_t *>(version_bitmap))[cur_nat_block / 8],
              (static_cast<uint8_t *>(pre_bitmap))[cur_nat_block / 8]);

    ASSERT_EQ(memcmp(pre_bitmap, version_bitmap, cp->nat_ver_bitmap_bytesize), 0);
  }

  memcpy(pre_bitmap, version_bitmap, cp->nat_ver_bitmap_bytesize);

  // 4. Creates inodes and triggers checkpoint
  // It creates 455 inodes in the root dir to make one dirty NAT block, and
  // it triggers checkpoint. It results in one bit triggered in NAT bitmap.
  // Since the current F2FS impl. supports only sync IO, every file creation results in
  // updating the root inode, and thus the first bit (root inode) in NAT bitmap is also triggered.
  for (int i = 0; i < 4; ++i) {
    CreateDirs(fs, 1, cp->checkpoint_ver * 10 + i);
    CreateFiles(fs, 100, cp->checkpoint_ver * 10 + i);
  }

  CreateDirs(fs, 1, cp->checkpoint_ver * 10 + 4);
  if (after_mkfs) {
    CreateFiles(fs, 46, cp->checkpoint_ver * 10 + 4);  // Mkfs uses 4 nids
  } else {
    CreateFiles(fs, 50, cp->checkpoint_ver * 10 + 4);  // 5 dirs + 450 files = 455
  }
}

void CheckpointTestSitBitmap(F2fs *fs, uint32_t expect_cp_position, uint32_t expect_cp_ver,
                             bool after_mkfs, uint8_t *&pre_bitmap) {
  LockedPage cp_page;
  uint8_t *version_bitmap;
  uint8_t cur_sit_bit = 0;

  // 1. Get last checkpoint
  GetLastCheckpoint(fs, expect_cp_position, after_mkfs, &cp_page);
  Checkpoint *cp = cp_page->GetAddress<Checkpoint>();
  ASSERT_EQ(cp->checkpoint_ver, expect_cp_ver);

  // 2. Get SIT version bitmap
  version_bitmap = static_cast<uint8_t *>(GetBitmapPtr(cp, MetaBitmap::kSitBitmap));
  ASSERT_NE(version_bitmap, nullptr);

  if (pre_bitmap == nullptr)
    pre_bitmap = new uint8_t[cp->sit_ver_bitmap_bytesize]();

  // 3. Validate version bitmap
  // Check dir and file inode version bitmap
  if (cp->checkpoint_ver == 2) {
    (static_cast<uint8_t *>(pre_bitmap))[2] |= kRootDirSitBit;
  }

  if (!after_mkfs) {
    auto cur_sit_block = cp->checkpoint_ver - 2;
    cur_sit_bit = 0x80 >> (cur_sit_block % 8);
    (static_cast<uint8_t *>(pre_bitmap))[cur_sit_block / 8] |= cur_sit_bit;

    ASSERT_EQ((static_cast<uint8_t *>(version_bitmap))[cur_sit_block / 8],
              (static_cast<uint8_t *>(pre_bitmap))[cur_sit_block / 8]);

    ASSERT_EQ(memcmp(pre_bitmap, version_bitmap, cp->sit_ver_bitmap_bytesize), 0);
  }

  memcpy(pre_bitmap, version_bitmap, cp->sit_ver_bitmap_bytesize);

  for (uint32_t i = 0; i < kMapPerSitEntry * kSitEntryPerBlock; ++i) {
    block_t new_blkaddr;
    if (after_mkfs && i < kMapPerSitEntry)
      continue;

    DoWriteSit(
        fs, &new_blkaddr, CursegType::kCursegWarmData,
        static_cast<uint32_t>((cp->checkpoint_ver - 1) * kSitEntryPerBlock + i / kMapPerSitEntry));
  }
}

void CheckpointTestAddOrphanInode(F2fs *fs, uint32_t expect_cp_position, uint32_t expect_cp_ver,
                                  bool after_mkfs) {
  LockedPage cp_page;

  // 1. Get last checkpoint
  GetLastCheckpoint(fs, expect_cp_position, after_mkfs, &cp_page);
  Checkpoint *cp = cp_page->GetAddress<Checkpoint>();
  ASSERT_EQ(cp->checkpoint_ver, expect_cp_ver);

  uint32_t orphan_inos = kOrphansPerBlock * kOrphanInodeBlockCnt;

  if (!after_mkfs) {
    auto start_ino = (cp->checkpoint_ver - 1) * orphan_inos;
    // 2. Get orphan inodes
    std::vector<uint32_t> cp_inos;
    std::vector<uint32_t> exp_inos(orphan_inos);
    std::iota(exp_inos.begin(), exp_inos.end(), start_ino);

    pgoff_t start_blk = cp_page->GetIndex() + 1;
    block_t orphan_blkaddr = cp->cp_pack_start_sum - 1;

    ASSERT_TRUE(fs->GetSuperblockInfo().TestCpFlags(CpFlag::kCpOrphanPresentFlag));

    for (auto ino : exp_inos) {
      fs->GetSuperblockInfo().RemoveVnodeFromVnodeSet(InoType::kOrphanIno, ino);
    }

    for (block_t i = 0; i < orphan_blkaddr; ++i) {
      LockedPage page;
      fs->GetMetaPage(start_blk + i, &page);
      OrphanBlock *orphan_blk;

      orphan_blk = page->GetAddress<OrphanBlock>();
      for (block_t j = 0; j < LeToCpu(orphan_blk->entry_count); ++j) {
        nid_t ino = LeToCpu(orphan_blk->ino[j]);
        cp_inos.push_back(ino);
      }
    }

    // 3. Check orphan inodes
    ASSERT_TRUE(std::equal(exp_inos.begin(), exp_inos.end(), cp_inos.begin()));
  }

  if (cp->checkpoint_ver > kCheckpointLoopCnt) {
    return;
  }

  // 4. Add shuffled orphan inodes for next checkpoint
  std::vector<uint32_t> inos(orphan_inos);
  auto start_ino = cp->checkpoint_ver * orphan_inos;
  std::iota(inos.begin(), inos.end(), start_ino);

  std::shuffle(inos.begin(), inos.end(),
               std::default_random_engine(static_cast<uint32_t>(cp->checkpoint_ver)));

  for (auto ino : inos) {
    fs->GetSuperblockInfo().AddVnodeToVnodeSet(InoType::kOrphanIno, ino);
  }

  ASSERT_EQ(fs->GetSuperblockInfo().GetVnodeSetSize(InoType::kOrphanIno), orphan_inos);

  // Add duplicate orphan inodes
  std::vector<uint32_t> dup_inos(orphan_inos / 10);
  std::generate(dup_inos.begin(), dup_inos.end(), [n = start_ino]() mutable {
    n += 10;
    return n - 10;
  });

  for (auto ino : dup_inos) {
    fs->GetSuperblockInfo().AddVnodeToVnodeSet(InoType::kOrphanIno, ino);
  }
}

void CheckpointTestRemoveOrphanInode(F2fs *fs, uint32_t expect_cp_position, uint32_t expect_cp_ver,
                                     bool after_mkfs) {
  LockedPage cp_page;

  // 1. Get last checkpoint
  GetLastCheckpoint(fs, expect_cp_position, after_mkfs, &cp_page);
  Checkpoint *cp = cp_page->GetAddress<Checkpoint>();
  ASSERT_EQ(cp->checkpoint_ver, expect_cp_ver);

  uint32_t orphan_inos = kOrphansPerBlock * kOrphanInodeBlockCnt;

  if (!after_mkfs) {
    auto start_ino = (cp->checkpoint_ver - 1) * orphan_inos;
    // 2. Get orphan inodes
    std::vector<uint32_t> cp_inos;
    std::vector<uint32_t> exp_inos(orphan_inos);
    std::iota(exp_inos.begin(), exp_inos.end(), start_ino);

    // Remove exp orphan inodes
    for (int i = safemath::checked_cast<int>(orphan_inos) / 10 - 1; i >= 0; --i) {
      uint32_t offset = i * 10;
      exp_inos.erase(exp_inos.begin() + offset);
    }

    pgoff_t start_blk = cp_page->GetIndex() + 1;
    block_t orphan_blkaddr = cp->cp_pack_start_sum - 1;

    ASSERT_TRUE(fs->GetSuperblockInfo().TestCpFlags(CpFlag::kCpOrphanPresentFlag));

    for (block_t i = 0; i < orphan_blkaddr; ++i) {
      LockedPage page;
      fs->GetMetaPage(start_blk + i, &page);
      OrphanBlock *orphan_blk;

      orphan_blk = page->GetAddress<OrphanBlock>();
      for (block_t j = 0; j < LeToCpu(orphan_blk->entry_count); ++j) {
        nid_t ino = LeToCpu(orphan_blk->ino[j]);
        cp_inos.push_back(ino);
        fs->GetSuperblockInfo().RemoveVnodeFromVnodeSet(InoType::kOrphanIno, ino);
      }
    }

    // 3. Check orphan inodes
    ASSERT_TRUE(std::equal(exp_inos.begin(), exp_inos.end(), cp_inos.begin()));
  }

  // 4. Add shuffled orphan inodes for next checkpoint
  std::vector<uint32_t> inos(orphan_inos);
  auto start_ino = cp->checkpoint_ver * orphan_inos;
  std::iota(inos.begin(), inos.end(), start_ino);

  std::shuffle(inos.begin(), inos.end(),
               std::default_random_engine(static_cast<uint32_t>(cp->checkpoint_ver)));

  if (cp->checkpoint_ver <= kCheckpointLoopCnt) {
    for (auto ino : inos) {
      fs->GetSuperblockInfo().AddVnodeToVnodeSet(InoType::kOrphanIno, ino);
    }
    ASSERT_EQ(fs->GetSuperblockInfo().GetVnodeSetSize(InoType::kOrphanIno), orphan_inos);
  }

  // 5. Remove orphan inodes
  std::vector<uint32_t> rm_inos(orphan_inos / 10);
  std::generate(rm_inos.begin(), rm_inos.end(), [n = start_ino]() mutable {
    n += 10;
    return n - 10;
  });

  for (auto ino : rm_inos) {
    fs->GetSuperblockInfo().RemoveVnodeFromVnodeSet(InoType::kOrphanIno, ino);
  }
}

void CheckpointTestRecoverOrphanInode(F2fs *fs, uint32_t expect_cp_position, uint32_t expect_cp_ver,
                                      bool after_mkfs,
                                      std::vector<fbl::RefPtr<VnodeF2fs>> &vnodes) {
  LockedPage cp_page;

  // 1. Get last checkpoint
  GetLastCheckpoint(fs, expect_cp_position, after_mkfs, &cp_page);
  Checkpoint *cp = cp_page->GetAddress<Checkpoint>();
  ASSERT_EQ(cp->checkpoint_ver, expect_cp_ver);

  uint32_t orphan_inos = kOrphansPerBlock;

  if (!after_mkfs) {
    // 2. Check recovery orphan inodes
    ASSERT_TRUE(fs->GetSuperblockInfo().TestCpFlags(CpFlag::kCpOrphanPresentFlag));
    ASSERT_EQ(vnodes.size(), orphan_inos);

    for (auto &vnode_refptr : vnodes) {
      ASSERT_EQ(vnode_refptr.get()->GetNlink(), (uint32_t)1);
    }

    ASSERT_EQ(fs->RecoverOrphanInodes(), 0);

    for (auto &vnode_refptr : vnodes) {
      ASSERT_EQ(vnode_refptr.get()->GetNlink(), (uint32_t)0);
      fs->GetSuperblockInfo().RemoveVnodeFromVnodeSet(InoType::kOrphanIno, vnode_refptr->GetKey());
      vnode_refptr.reset();
    }
    vnodes.clear();
    vnodes.shrink_to_fit();
  }

  if (cp->checkpoint_ver > kCheckpointLoopCnt) {
    return;
  }

  // 3. Add shuffled orphan inodes for next checkpoint
  std::vector<uint32_t> inos(orphan_inos);
  auto start_ino = cp->checkpoint_ver * orphan_inos;
  std::iota(inos.begin(), inos.end(), start_ino);

  std::shuffle(inos.begin(), inos.end(),
               std::default_random_engine(static_cast<uint32_t>(cp->checkpoint_ver)));

  for (auto ino : inos) {
    fbl::RefPtr<VnodeF2fs> vnode_refptr;
    VnodeF2fs *vnode = nullptr;

    VnodeF2fs::Allocate(fs, ino, S_IFREG, &vnode_refptr);
    ASSERT_NE(vnode = vnode_refptr.get(), nullptr);

    vnode->ClearNlink();
    vnode->IncNlink();
    vnode->UnlockNewInode();

    fs->InsertVnode(vnode);

    vnodes.push_back(std::move(vnode_refptr));
    fs->GetSuperblockInfo().AddVnodeToVnodeSet(InoType::kOrphanIno, ino);
    vnode_refptr.reset();
  }

  ASSERT_EQ(fs->GetSuperblockInfo().GetVnodeSetSize(InoType::kOrphanIno), orphan_inos);
}

void CheckpointTestCompactedSummaries(F2fs *fs, uint32_t expect_cp_position, uint32_t expect_cp_ver,
                                      bool after_mkfs) {
  LockedPage cp_page;

  // 1. Get last checkpoint
  GetLastCheckpoint(fs, expect_cp_position, after_mkfs, &cp_page);
  Checkpoint *cp = cp_page->GetAddress<Checkpoint>();
  ASSERT_EQ(cp->checkpoint_ver, expect_cp_ver);

  if (!after_mkfs) {
    // 2. Clear current segment summaries
    for (int i = static_cast<int>(CursegType::kCursegHotData);
         i <= static_cast<int>(CursegType::kCursegColdData); ++i) {
      CursegInfo *curseg = fs->GetSegmentManager().CURSEG_I(static_cast<CursegType>(i));
      for (auto &entrie : curseg->sum_blk->entries) {
        entrie.nid = 0;
        entrie.version = 0;
        entrie.ofs_in_node = 0;
      }
    }

    // 3. Recover compacted data summaries
    ASSERT_TRUE(fs->GetSuperblockInfo().TestCpFlags(CpFlag::kCpCompactSumFlag));
    ASSERT_EQ(fs->GetSegmentManager().ReadCompactedSummaries(), 0);

    // 4. Check recovered active summary info
    for (int i = static_cast<int>(CursegType::kCursegHotData);
         i <= static_cast<int>(CursegType::kCursegColdData); ++i) {
      CursegInfo *curseg = fs->GetSegmentManager().CURSEG_I(static_cast<CursegType>(i));

      if (cp->checkpoint_ver > 3)  // cp_ver 2 and 3 have random segno
        ASSERT_EQ(curseg->segno, (cp->checkpoint_ver - 3) * 3 + i + 1);
      ASSERT_EQ(curseg->next_blkoff, 256);

      for (uint32_t j = 0; j < kEntriesInSum / 2; ++j) {
        if (cp->checkpoint_ver == 2 &&
            IsRootInode(static_cast<CursegType>(i), j))  // root inode dentry
          continue;

        nid_t nid = curseg->sum_blk->entries[j].nid;
        ASSERT_EQ(nid, (uint32_t)3);
        ASSERT_EQ(static_cast<uint64_t>(curseg->sum_blk->entries[j].version),
                  cp->checkpoint_ver - 1);
        uint16_t ofs_in_node = curseg->sum_blk->entries[j].ofs_in_node;
        ASSERT_EQ(ofs_in_node, j);
      }
    }
  }

  // 5. Fill compact data summary
  // Close and change current active segment
  // Fill current active segments for compacted data summaries
  for (int i = static_cast<int>(CursegType::kCursegHotData);
       i <= static_cast<int>(CursegType::kCursegColdData); ++i) {
    // Close previous segment
    if (!after_mkfs) {
      for (uint32_t j = 0; j < kEntriesInSum / 2; ++j) {
        block_t new_blkaddr;
        DoWriteSit(fs, &new_blkaddr, static_cast<CursegType>(i), kNullSegNo);
      }
    }

    // Write workload
    for (uint16_t j = 0; j < kEntriesInSum / 2; ++j) {
      block_t new_blkaddr;
      Summary sum;

      if (cp->checkpoint_ver == 1 &&
          IsRootInode(static_cast<CursegType>(i), j))  // root inode dentry
        continue;

      fs->GetSegmentManager().SetSummary(&sum, 3, j, static_cast<uint8_t>(cp->checkpoint_ver));
      fs->GetSegmentManager().AddSumEntry(static_cast<CursegType>(i), &sum, j);

      DoWriteSit(fs, &new_blkaddr, static_cast<CursegType>(i), kNullSegNo);
    }
  }
  ASSERT_LT(fs->GetSegmentManager().NpagesForSummaryFlush(), 3);
}

void CheckpointTestNormalSummaries(F2fs *fs, uint32_t expect_cp_position, uint32_t expect_cp_ver,
                                   bool after_mkfs) {
  LockedPage cp_page;

  // 1. Get last checkpoint
  GetLastCheckpoint(fs, expect_cp_position, after_mkfs, &cp_page);
  Checkpoint *cp = cp_page->GetAddress<Checkpoint>();
  ASSERT_EQ(cp->checkpoint_ver, expect_cp_ver);

  if (!after_mkfs) {
    // 2. Clear current segment summaries
    for (int i = static_cast<int>(CursegType::kCursegHotData);
         i <= static_cast<int>(CursegType::kCursegColdNode); ++i) {
      CursegInfo *curseg = fs->GetSegmentManager().CURSEG_I(static_cast<CursegType>(i));
      for (auto &entrie : curseg->sum_blk->entries) {
        entrie.nid = 0;
        entrie.version = 0;
        entrie.ofs_in_node = 0;
      }
    }

    // 2. Recover normal data summary
    ASSERT_FALSE(fs->GetSuperblockInfo().TestCpFlags(CpFlag::kCpCompactSumFlag));
    for (int type = static_cast<int>(CursegType::kCursegHotData);
         type <= static_cast<int>(CursegType::kCursegColdNode); ++type) {
      ASSERT_EQ(fs->GetSegmentManager().ReadNormalSummaries(type), ZX_OK);
    }

    // 4. Check recovered active summary info
    for (int i = static_cast<int>(CursegType::kCursegHotData);
         i <= static_cast<int>(CursegType::kCursegColdNode); ++i) {
      CursegInfo *curseg = fs->GetSegmentManager().CURSEG_I(static_cast<CursegType>(i));

      if (cp->checkpoint_ver > 3)  // cp_ver 2 and 3 have random segno
        ASSERT_EQ(curseg->segno, (cp->checkpoint_ver - 3) * 6 + i + 1);
      ASSERT_EQ(curseg->next_blkoff, 512);

      for (uint32_t j = 0; j < kEntriesInSum; ++j) {
        if (cp->checkpoint_ver == 2 && IsRootInode(static_cast<CursegType>(i), j))  // root inode
          continue;

        nid_t nid = curseg->sum_blk->entries[j].nid;
        ASSERT_EQ(nid, cp->checkpoint_ver - 1);
        if (!fs->GetSegmentManager().IsNodeSeg(static_cast<CursegType>(i))) {
          ASSERT_EQ(static_cast<uint64_t>(curseg->sum_blk->entries[j].version),
                    cp->checkpoint_ver - 1);
          uint16_t ofs_in_node = curseg->sum_blk->entries[j].ofs_in_node;
          ASSERT_EQ(ofs_in_node, j);
        }
      }
    }
  }

  // 3. Fill normal data summary
  // Close and change current active segment
  // Fill current active segments for normal summaries
  for (int i = static_cast<int>(CursegType::kCursegHotData);
       i <= static_cast<int>(CursegType::kCursegColdNode); ++i) {
    for (uint16_t j = 0; j < kEntriesInSum; ++j) {
      block_t new_blkaddr;
      Summary sum;

      if (cp->checkpoint_ver == 1 && IsRootInode(static_cast<CursegType>(i), j))
        continue;

      fs->GetSegmentManager().SetSummary(&sum, static_cast<nid_t>(cp->checkpoint_ver), j,
                                         static_cast<uint8_t>(cp->checkpoint_ver));
      fs->GetSegmentManager().AddSumEntry(static_cast<CursegType>(i), &sum, j);

      DoWriteSit(fs, &new_blkaddr, static_cast<CursegType>(i), kNullSegNo);
    }
  }
  ASSERT_GT(fs->GetSegmentManager().NpagesForSummaryFlush(), 2);
}

void CheckpointTestSitJournal(F2fs *fs, uint32_t expect_cp_position, uint32_t expect_cp_ver,
                              bool after_mkfs, std::vector<uint32_t> &segnos) {
  LockedPage cp_page;

  // 1. Get last checkpoint
  GetLastCheckpoint(fs, expect_cp_position, after_mkfs, &cp_page);
  Checkpoint *cp = cp_page->GetAddress<Checkpoint>();
  ASSERT_EQ(cp->checkpoint_ver, expect_cp_ver);

  if (!after_mkfs) {
    // 2. Recover compacted data summaries
    ASSERT_TRUE(fs->GetSuperblockInfo().TestCpFlags(CpFlag::kCpCompactSumFlag));
    ASSERT_EQ(fs->GetSegmentManager().ReadCompactedSummaries(), 0);

    // 3. Check recovered journal
    CursegInfo *curseg = fs->GetSegmentManager().CURSEG_I(CursegType::kCursegColdData);

    SummaryBlock *sum = curseg->sum_blk;
    for (int i = 0; i < SitsInCursum(sum); ++i) {
      uint32_t segno = LeToCpu(SegnoInJournal(sum, i));
      ASSERT_EQ(segno, segnos[i]);
    }
  }

  // 4. Fill compact data summary
  if (!after_mkfs) {
    CursegInfo *curseg = fs->GetSegmentManager().CURSEG_I(CursegType::kCursegColdData);

    // Clear SIT journal
    if (SitsInCursum(curseg->sum_blk) >= static_cast<int>(kSitJournalEntries)) {
      SitInfo &sit_i = fs->GetSegmentManager().GetSitInfo();
      uint8_t *bitmap = sit_i.dirty_sentries_bitmap.get();
      block_t nsegs = fs->GetSegmentManager().TotalSegs();
      uint32_t segno = -1;

      // Add dummy dirty sentries
      for (uint32_t i = 0; i < kMapPerSitEntry; ++i) {
        block_t new_blkaddr;
        DoWriteSit(fs, &new_blkaddr, CursegType::kCursegColdData, kNullSegNo);
      }

      // Move journal sentries to dirty sentries
      ASSERT_TRUE(fs->GetSegmentManager().FlushSitsInJournal());

      // Clear dirty sentries
      while ((segno = FindNextBit(bitmap, nsegs, segno + 1)) < nsegs) {
        ClearBit(segno, bitmap);
        --sit_i.dirty_sentries;
      }
    }
  }
  segnos.clear();
  segnos.shrink_to_fit();

  // Fill SIT journal
  for (uint32_t i = 0; i < kSitJournalEntries * kMapPerSitEntry; ++i) {
    block_t new_blkaddr;
    DoWriteSit(fs, &new_blkaddr, CursegType::kCursegColdData, kNullSegNo);
    CursegInfo *curseg = fs->GetSegmentManager().CURSEG_I(CursegType::kCursegColdData);
    if (curseg->next_blkoff == 1) {
      segnos.push_back(curseg->segno);
    }
  }
  ASSERT_LT(fs->GetSegmentManager().NpagesForSummaryFlush(), 3);
}

void CheckpointTestNatJournal(F2fs *fs, uint32_t expect_cp_position, uint32_t expect_cp_ver,
                              bool after_mkfs, std::vector<uint32_t> &nids) {
  SuperblockInfo &superblock_info = fs->GetSuperblockInfo();
  NodeManager &node_manager = fs->GetNodeManager();
  LockedPage cp_page;
  CursegInfo *curseg = fs->GetSegmentManager().CURSEG_I(CursegType::kCursegHotData);

  // 1. Get last checkpoint
  GetLastCheckpoint(fs, expect_cp_position, after_mkfs, &cp_page);
  Checkpoint *cp = cp_page->GetAddress<Checkpoint>();
  ASSERT_EQ(cp->checkpoint_ver, expect_cp_ver);

  if (!after_mkfs) {
    // 2. Recover compacted data summaries
    ASSERT_TRUE(fs->GetSuperblockInfo().TestCpFlags(CpFlag::kCpCompactSumFlag));
    ASSERT_EQ(fs->GetSegmentManager().ReadCompactedSummaries(), 0);

    // 3. Check recovered journal
    SummaryBlock *sum = curseg->sum_blk;
    for (int i = 0; i < NatsInCursum(sum); ++i) {
      ASSERT_EQ(NidInJournal(sum, i), nids[i]);
      ASSERT_EQ(NatInJournal(sum, i).version, cp->checkpoint_ver - 1);
    }
  }

  // 4. Fill compact data summary
  if (!after_mkfs) {
    // Clear NAT journal
    if (NatsInCursum(curseg->sum_blk) >= static_cast<int>(kNatJournalEntries)) {
      // Add dummy dirty NAT entries
      MapTester::DoWriteNat(fs, kNatJournalEntries + superblock_info.GetRootIno() + 1,
                            kNatJournalEntries, static_cast<uint8_t>(cp->checkpoint_ver));

      // Move journal sentries to dirty sentries
      ASSERT_TRUE(fs->GetNodeManager().FlushNatsInJournal());

      // Clear dirty sentries
      MapTester::ClearAllDirtyNatEntries(node_manager);
    }
  }
  nids.clear();
  nids.shrink_to_fit();

  // Fill NAT journal
  for (uint32_t i = superblock_info.GetRootIno() + 1;
       i < kNatJournalEntries + superblock_info.GetRootIno() + 1; ++i) {
    MapTester::DoWriteNat(fs, i, i, static_cast<uint8_t>(cp->checkpoint_ver));
    nids.push_back(i);
  }

  ASSERT_LT(fs->GetSegmentManager().NpagesForSummaryFlush(), 3);

  // Flush NAT cache
  MapTester::RemoveAllNatEntries(node_manager);
}

void CheckpointTestMain(uint32_t test) {
  std::unique_ptr<f2fs::Bcache> bc;
  MountOptions options;
  bool after_mkfs = true;
  int checkpoint_pack = kCheckpointPack0;
  uint8_t *pre_bitmap = nullptr;
  std::vector<fbl::RefPtr<VnodeF2fs>> vnodes;
  std::vector<uint32_t> prev_values;
  std::unique_ptr<F2fs> fs;

  FileTester::MkfsOnFakeDev(&bc, kBlockCount);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  // Disable roll-forward recovery
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptDisableRollForward), 1), ZX_OK);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);

  // Validate checkpoint
  for (uint32_t i = 1; i <= kCheckpointLoopCnt + 1; ++i) {
    if (!after_mkfs)
      fs->WriteCheckpoint(false, true);

    switch (test) {
      case kCheckpointVersionTest:
        CheckpointTestVersion(fs.get(), checkpoint_pack, i, after_mkfs);
        break;
      case kCheckpointNatBitmapTest:
        CheckpointTestNatBitmap(fs.get(), checkpoint_pack, i, after_mkfs, pre_bitmap);
        break;
      case kCheckpointSitBitmapTest:
        CheckpointTestSitBitmap(fs.get(), checkpoint_pack, i, after_mkfs, pre_bitmap);
        break;
      case kCheckpointAddOrphanInodeTest:
        CheckpointTestAddOrphanInode(fs.get(), checkpoint_pack, i, after_mkfs);
        break;
      case kCheckpointRemoveOrphanInodeTest:
        CheckpointTestRemoveOrphanInode(fs.get(), checkpoint_pack, i, after_mkfs);
        break;
      case kCheckpointRecoverOrphanInodeTest:
        CheckpointTestRecoverOrphanInode(fs.get(), checkpoint_pack, i, after_mkfs, vnodes);
        break;
      case kCheckpointCompactedSummariesTest:
        CheckpointTestCompactedSummaries(fs.get(), checkpoint_pack, i, after_mkfs);
        break;
      case kCheckpointNormalSummariesTest:
        CheckpointTestNormalSummaries(fs.get(), checkpoint_pack, i, after_mkfs);
        break;
      case kCheckpointSitJournalTest:
        CheckpointTestSitJournal(fs.get(), checkpoint_pack, i, after_mkfs, prev_values);
        break;
      case kCheckpointNatJournalTest:
        CheckpointTestNatJournal(fs.get(), checkpoint_pack, i, after_mkfs, prev_values);
        break;
      default:
        ASSERT_EQ(0, 1);
        break;
    };

    if (after_mkfs)
      after_mkfs = false;

    if (checkpoint_pack == kCheckpointPack0) {
      checkpoint_pack = kCheckpointPack1;
    } else {
      checkpoint_pack = kCheckpointPack0;
    }
  }
  ASSERT_EQ(root->Close(), ZX_OK);
  root = nullptr;

  FileTester::Unmount(std::move(fs), &bc);

  delete[] pre_bitmap;
}

TEST(CheckpointTest, Version) { CheckpointTestMain(kCheckpointVersionTest); }

TEST(CheckpointTest, NatBitmap) { CheckpointTestMain(kCheckpointNatBitmapTest); }

TEST(CheckpointTest, SitBitmap) { CheckpointTestMain(kCheckpointSitBitmapTest); }

TEST(CheckpointTest, AddOrphanInode) { CheckpointTestMain(kCheckpointAddOrphanInodeTest); }

TEST(CheckpointTest, RemoveOrphanInode) { CheckpointTestMain(kCheckpointRemoveOrphanInodeTest); }

TEST(CheckpointTest, RecoverOrphanInode) { CheckpointTestMain(kCheckpointRecoverOrphanInodeTest); }

TEST(CheckpointTest, CompactedSummaries) { CheckpointTestMain(kCheckpointCompactedSummariesTest); }

TEST(CheckpointTest, NormalSummaries) { CheckpointTestMain(kCheckpointNormalSummariesTest); }

TEST(CheckpointTest, SitJournal) { CheckpointTestMain(kCheckpointSitJournalTest); }

TEST(CheckpointTest, NatJournal) { CheckpointTestMain(kCheckpointNatJournalTest); }

TEST(CheckpointTest, UmountFlag) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  // create f2fs and root dir
  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);
  SuperblockInfo &superblock_info = fs->GetSuperblockInfo();

  // read the node block where the root inode is stored
  {
    LockedPage root_node_page;
    fs->GetNodeManager().GetNodePage(superblock_info.GetRootIno(), &root_node_page);
    ASSERT_TRUE(root_node_page);
  }

  ASSERT_EQ(root->Close(), ZX_OK);
  root = nullptr;

  fs->WriteCheckpoint(false, true);
  FileTester::SuddenPowerOff(std::move(fs), &bc);

  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);
  fs->WriteCheckpoint(false, false);
  FileTester::SuddenPowerOff(std::move(fs), &bc);

  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);
  FileTester::Unmount(std::move(fs), &bc);
}

TEST_F(CheckpointTestF, CpError) {
  fbl::RefPtr<fs::Vnode> test_file;
  root_dir_->Create("test", S_IFREG, &test_file);
  fbl::RefPtr<f2fs::File> vn = fbl::RefPtr<f2fs::File>::Downcast(std::move(test_file));
  char wbuf[] = "Checkpoint error test";
  char rbuf[kBlockSize];

  // Make dirty data, node, and meta Pages.
  FileTester::AppendToFile(vn.get(), wbuf, sizeof(wbuf));

  // The appended data is written in the node page of |root_dir_|
  // since the inline_data option is enabled by default.
  ASSERT_EQ(fs_->GetSuperblockInfo().GetPageCount(CountType::kDirtyNodes), 1);
  ASSERT_FALSE(fs_->GetSuperblockInfo().TestCpFlags(CpFlag::kCpErrorFlag));

  // Set a hook to trigger an io error with any write requests on FakeBlockDevice,
  // which causes that f2fs sets the checkpoint error flag.
  auto hook = [](const block_fifo_request_t &_req, const zx::vmo *_vmo) {
    if (_req.opcode == BLOCKIO_WRITE) {
      return ZX_ERR_IO;
    }
    return ZX_OK;
  };
  static_cast<FakeBlockDevice *>(fs_->GetBc().GetDevice())->set_hook(std::move(hook));
  fs_->WriteCheckpoint(false, false);

  ASSERT_TRUE(fs_->GetSuperblockInfo().TestCpFlags(CpFlag::kCpErrorFlag));

  // All operations causing dirty pages are not allowed.
  size_t end = 0, out = 0;
  ASSERT_EQ(vn->Append(wbuf, kBlockSize, &end, &out), ZX_ERR_BAD_STATE);
  ASSERT_EQ(vn->Write(wbuf, kBlockSize, 0, &out), ZX_ERR_BAD_STATE);
  ASSERT_EQ(vn->Truncate(0), ZX_ERR_BAD_STATE);
  ASSERT_EQ(root_dir_->Unlink("test", false), ZX_ERR_BAD_STATE);
  ASSERT_EQ(root_dir_->Create("test2", S_IFREG, &test_file), ZX_ERR_BAD_STATE);
  ASSERT_EQ(root_dir_->Rename(root_dir_, "test", "test1", false, false), ZX_ERR_BAD_STATE);
  ASSERT_EQ(root_dir_->Link("test", vn), ZX_ERR_BAD_STATE);

  // Read operations should succeed.
  FileTester::ReadFromFile(vn.get(), rbuf, sizeof(wbuf), 0);
  ASSERT_EQ(root_dir_->Lookup("test", &test_file), ZX_OK);
  ASSERT_EQ(strcmp(wbuf, rbuf), 0);
  static_cast<FakeBlockDevice *>(fs_->GetBc().GetDevice())->set_hook(nullptr);

  vn->Close();
  vn = nullptr;
}

}  // namespace
}  // namespace f2fs
