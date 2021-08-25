// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/macros.h>

#include <random>
#include <vector>

#include <block-client/cpp/fake-device.h>
#include <gtest/gtest.h>

#include "src/storage/f2fs/f2fs.h"
#include "unit_lib.h"

namespace f2fs {
namespace {

using block_client::FakeBlockDevice;

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

void ReadCheckpoint(F2fs *fs, block_t cp_addr, Page **cp_out) {
  Page *cp_page[2];  // cp_page[0]: header, cp_page[1]: footer
  SbInfo &sbi = fs->GetSbInfo();
  uint64_t blk_size = sbi.blocksize;
  Checkpoint *cp_block;
  uint64_t version[2];  // version[0]: header, version[1]: footer
  uint32_t crc;
  size_t crc_offset;

  for (int i = 0; i < 2; i++) {
    // Read checkpoint pack header/footer
    cp_page[i] = fs->GetMetaPage(cp_addr);
    ASSERT_NE(cp_page[i], nullptr);
    // Check header CRC
    cp_block = static_cast<Checkpoint *>(PageAddress(cp_page[i]));
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

  F2fsPutPage(cp_page[1], 1);

  *cp_out = cp_page[0];
}

void GetLastCheckpoint(F2fs *fs, uint32_t expect_cp_position, bool after_mkfs, Page **cp_out) {
  SuperBlock &fsb = fs->RawSb();
  Checkpoint *cp_block1 = nullptr, *cp_block2 = nullptr, *cur_cp_block = nullptr;
  Page *cp_page1 = nullptr, *cp_page2 = nullptr, *cur_cp_page = nullptr;
  block_t cp_addr;
  uint32_t cp_position = 0;

  cp_addr = LeToCpu(fsb.cp_blkaddr);
  ReadCheckpoint(fs, cp_addr, &cp_page1);
  cp_block1 = static_cast<Checkpoint *>(PageAddress(cp_page1));

  if (!after_mkfs) {
    cp_addr += 1 << LeToCpu(fsb.log_blocks_per_seg);
    ReadCheckpoint(fs, cp_addr, &cp_page2);
    cp_block2 = static_cast<Checkpoint *>(PageAddress(cp_page2));
  }

  if (after_mkfs) {
    cur_cp_page = cp_page1;
    cur_cp_block = cp_block1;
    cp_position = kCheckpointPack0;
  } else if (cp_block1 && cp_block2) {
    if (VerAfter(cp_block2->checkpoint_ver, cp_block1->checkpoint_ver)) {
      cur_cp_page = cp_page2;
      cur_cp_block = cp_block2;
      cp_position = kCheckpointPack1;
      ASSERT_EQ(cp_block1->checkpoint_ver, cp_block2->checkpoint_ver - 1);
    } else {
      cur_cp_page = cp_page1;
      cur_cp_block = cp_block1;
      cp_position = kCheckpointPack0;
      ASSERT_EQ(cp_block2->checkpoint_ver, cp_block1->checkpoint_ver - 1);
    }
  } else {
    ASSERT_EQ(0, 1);
  }

  FX_LOGS(INFO) << "CP[" << cp_position << "] Version = " << cur_cp_block->checkpoint_ver;
  ASSERT_EQ(cp_position, expect_cp_position);

  *cp_out = cur_cp_page;

  if (!after_mkfs) {
    if (cp_position == kCheckpointPack0) {
      F2fsPutPage(cp_page2, 1);
    } else {
      F2fsPutPage(cp_page1, 1);
    }
  }
}

inline void *GetBitmapPrt(Checkpoint *ckpt, MetaBitmap flag) {
  uint32_t offset = (flag == MetaBitmap::kNatBitmap) ? ckpt->sit_ver_bitmap_bytesize : 0;
  return &ckpt->sit_nat_version_bitmap + offset;
}

void CreateDirs(F2fs *fs, int dir_cnt, uint64_t version) {
  fbl::RefPtr<VnodeF2fs> data_root;
  ASSERT_EQ(VnodeF2fs::Vget(fs, fs->RawSb().root_ino, &data_root), ZX_OK);
  Dir *root_dir = static_cast<Dir *>(data_root.get());
  std::string filename;

  for (int i = 0; i < dir_cnt; i++) {
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

  for (int i = 0; i < file_cnt; i++) {
    fbl::RefPtr<fs::Vnode> vnode;
    filename = "file_" + std::to_string(version) + "_" + std::to_string(i);
    ASSERT_EQ(root_dir->Create(filename.c_str(), S_IFREG, &vnode), ZX_OK);
    vnode->Close();
    vnode.reset();
  }
}

void DoWriteSit(F2fs *fs, block_t *new_blkaddr, CursegType type, uint32_t exp_segno) {
  SbInfo &sbi = fs->GetSbInfo();
  SitInfo *sit_i = GetSitInfo(&sbi);

  if (!fs->Segmgr().HasCursegSpace(type)) {
    fs->Segmgr().AllocateSegmentByDefault(type, false);
  }

  CursegInfo *curseg = SegMgr::CURSEG_I(&sbi, type);
  if (exp_segno != kNullSegNo)
    ASSERT_EQ(curseg->segno, exp_segno);

  fbl::AutoLock curseg_lock(&curseg->curseg_mutex);
  *new_blkaddr = NextFreeBlkAddr(&sbi, curseg);
  uint32_t old_cursegno = curseg->segno;

  fbl::AutoLock sentry_lock(&sit_i->sentry_lock);
  fs->Segmgr().RefreshNextBlkoff(curseg);
  sbi.block_count[curseg->alloc_type]++;

  fs->Segmgr().RefreshSitEntry(kNullSegNo, *new_blkaddr);
  fs->Segmgr().LocateDirtySegment(old_cursegno);
}

void DoWriteNat(F2fs *fs, nid_t nid, block_t blkaddr, uint8_t version) {
  SbInfo &sbi = fs->GetSbInfo();
  NmInfo *nm_i = GetNmInfo(&sbi);
  NatEntry *ne = new NatEntry;

  memset(ne, 0, sizeof(NatEntry));
  NatSetNid(ne, nid);
  list_add_tail(&nm_i->nat_entries, &ne->list);
  nm_i->nat_cnt++;

  ne->checkpointed = false;
  NatSetBlkaddr(ne, blkaddr);
  NatSetVersion(ne, version);
  list_move_tail(&nm_i->dirty_nat_entries, &ne->list);
}

bool IsRootInode(CursegType curseg_type, uint32_t offset) {
  return (curseg_type == CursegType::kCursegHotData || curseg_type == CursegType::kCursegHotNode) &&
         offset == 0;
}

void CheckpointTestVersion(F2fs *fs, uint32_t expect_cp_position, uint32_t expect_cp_ver,
                           bool after_mkfs) {
  Checkpoint *cp = nullptr;
  Page *cp_page = nullptr;

  GetLastCheckpoint(fs, expect_cp_position, after_mkfs, &cp_page);
  cp = static_cast<Checkpoint *>(PageAddress(cp_page));

  ASSERT_EQ(cp->checkpoint_ver, expect_cp_ver);

  F2fsPutPage(cp_page, 1);
}

void CheckpointTestNatBitmap(F2fs *fs, uint32_t expect_cp_position, uint32_t expect_cp_ver,
                             bool after_mkfs, uint8_t *&pre_bitmap) {
  Page *cp_page = nullptr;
  uint8_t *version_bitmap;
  uint32_t cur_nat_block = 0;
  uint8_t cur_nat_bit = 0;

  // 1. Get last checkpoint
  GetLastCheckpoint(fs, expect_cp_position, after_mkfs, &cp_page);
  Checkpoint *cp = static_cast<Checkpoint *>(PageAddress(cp_page));
  ASSERT_EQ(cp->checkpoint_ver, expect_cp_ver);

  // 2. Get NAT version bitmap
  version_bitmap = static_cast<uint8_t *>(GetBitmapPrt(cp, MetaBitmap::kNatBitmap));
  ASSERT_NE(version_bitmap, nullptr);

  if (pre_bitmap == nullptr)
    pre_bitmap = new uint8_t[cp->nat_ver_bitmap_bytesize]();

  std::string bitmapPrint;
  for (uint32_t i = 0; i < 8 /*cp->nat_ver_bitmap_bytesize*/; i++) {
    bitmapPrint += std::bitset<8>((static_cast<uint8_t *>(pre_bitmap))[i]).to_string();
  }
  FX_LOGS(DEBUG) << std::dec << "CP ver= " << cp->checkpoint_ver
                 << ",     pre_bitmap = " << bitmapPrint;

  bitmapPrint.clear();
  for (uint32_t i = 0; i < 8 /*cp->nat_ver_bitmap_bytesize*/; i++) {
    bitmapPrint += std::bitset<8>((static_cast<uint8_t *>(version_bitmap))[i]).to_string();
  }
  FX_LOGS(DEBUG) << "CP ver= " << cp->checkpoint_ver << ", version_bitmap = " << bitmapPrint;

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

    cur_nat_block = cp->checkpoint_ver - 2;
    cur_nat_bit = 0x80 >> (cur_nat_block % 8);
    (static_cast<uint8_t *>(pre_bitmap))[cur_nat_block / 8] |= cur_nat_bit;

    ASSERT_EQ((static_cast<uint8_t *>(version_bitmap))[cur_nat_block / 8],
              (static_cast<uint8_t *>(pre_bitmap))[cur_nat_block / 8]);

    for (uint32_t i = 0; i < 8 /*cp->nat_ver_bitmap_bytesize*/; i++) {
      bitmapPrint += std::bitset<8>((static_cast<uint8_t *>(pre_bitmap))[i]).to_string();
    }
    FX_LOGS(DEBUG) << std::dec << "CP ver= " << cp->checkpoint_ver
                   << ", exp pre_bitmap = " << bitmapPrint;

    ASSERT_EQ(memcmp(pre_bitmap, version_bitmap, cp->nat_ver_bitmap_bytesize), 0);
  }

  memcpy(pre_bitmap, version_bitmap, cp->nat_ver_bitmap_bytesize);

  // 4. Creates inodes and triggers checkpoint
  // It creates 455 inodes in the root dir to make one dirty NAT block, and
  // it triggers checkpoint. It results in one bit triggered in NAT bitmap.
  // Since the current F2FS impl. supports only sync IO, every file creation results in
  // updating the root inode, and thus the first bit (root inode) in NAT bitmap is also triggered.
  for (int i = 0; i < 4; i++) {
    CreateDirs(fs, 1, cp->checkpoint_ver * 10 + i);
    CreateFiles(fs, 100, cp->checkpoint_ver * 10 + i);
  }

  CreateDirs(fs, 1, cp->checkpoint_ver * 10 + 4);
  if (after_mkfs) {
    CreateFiles(fs, 46, cp->checkpoint_ver * 10 + 4);  // Mkfs uses 4 nids
  } else {
    CreateFiles(fs, 50, cp->checkpoint_ver * 10 + 4);  // 5 dirs + 450 files = 455
  }

  F2fsPutPage(cp_page, 1);
}

void CheckpointTestSitBitmap(F2fs *fs, uint32_t expect_cp_position, uint32_t expect_cp_ver,
                             bool after_mkfs, uint8_t *&pre_bitmap) {
  Page *cp_page = nullptr;
  uint8_t *version_bitmap;
  uint32_t cur_sit_block = 0;
  uint8_t cur_sit_bit = 0;

  // 1. Get last checkpoint
  GetLastCheckpoint(fs, expect_cp_position, after_mkfs, &cp_page);
  Checkpoint *cp = static_cast<Checkpoint *>(PageAddress(cp_page));
  ASSERT_EQ(cp->checkpoint_ver, expect_cp_ver);

  // 2. Get SIT version bitmap
  version_bitmap = static_cast<uint8_t *>(GetBitmapPrt(cp, MetaBitmap::kSitBitmap));
  ASSERT_NE(version_bitmap, nullptr);

  if (pre_bitmap == nullptr)
    pre_bitmap = new uint8_t[cp->sit_ver_bitmap_bytesize]();

  std::string bitmapPrint;
  for (uint32_t i = 0; i < 8 /*cp->nat_ver_bitmap_bytesize*/; i++) {
    bitmapPrint += std::bitset<8>((static_cast<uint8_t *>(pre_bitmap))[i]).to_string();
  }
  FX_LOGS(DEBUG) << std::dec << "CP ver= " << cp->checkpoint_ver
                 << ",     pre_bitmap = " << bitmapPrint;

  bitmapPrint.clear();
  for (uint32_t i = 0; i < 8 /*cp->nat_ver_bitmap_bytesize*/; i++) {
    bitmapPrint += std::bitset<8>((static_cast<uint8_t *>(version_bitmap))[i]).to_string();
  }
  FX_LOGS(DEBUG) << "CP ver= " << cp->checkpoint_ver << ", version_bitmap = " << bitmapPrint;

  // 3. Validate version bitmap
  // Check dir and file inode version bitmap
  if (cp->checkpoint_ver == 2) {
    (static_cast<uint8_t *>(pre_bitmap))[2] |= kRootDirSitBit;
  }

  if (!after_mkfs) {
    cur_sit_block = cp->checkpoint_ver - 2;
    cur_sit_bit = 0x80 >> (cur_sit_block % 8);
    (static_cast<uint8_t *>(pre_bitmap))[cur_sit_block / 8] |= cur_sit_bit;

    ASSERT_EQ((static_cast<uint8_t *>(version_bitmap))[cur_sit_block / 8],
              (static_cast<uint8_t *>(pre_bitmap))[cur_sit_block / 8]);

    for (uint32_t i = 0; i < 8 /*cp->nat_ver_bitmap_bytesize*/; i++) {
      bitmapPrint += std::bitset<8>((static_cast<uint8_t *>(pre_bitmap))[i]).to_string();
    }
    FX_LOGS(DEBUG) << std::dec << "CP ver= " << cp->checkpoint_ver
                   << ", exp pre_bitmap = " << bitmapPrint;

    ASSERT_EQ(memcmp(pre_bitmap, version_bitmap, cp->sit_ver_bitmap_bytesize), 0);
  }

  memcpy(pre_bitmap, version_bitmap, cp->sit_ver_bitmap_bytesize);

  for (uint32_t i = 0; i < kMapPerSitEntry * kSitEntryPerBlock; i++) {
    block_t new_blkaddr;
    if (after_mkfs && i < kMapPerSitEntry)
      continue;

    DoWriteSit(fs, &new_blkaddr, CursegType::kCursegWarmData,
               (cp->checkpoint_ver - 1) * kSitEntryPerBlock + i / kMapPerSitEntry);
  }

  F2fsPutPage(cp_page, 1);
}

void CheckpointTestAddOrphanInode(F2fs *fs, uint32_t expect_cp_position, uint32_t expect_cp_ver,
                                  bool after_mkfs) {
  Page *cp_page = nullptr;

  // 1. Get last checkpoint
  GetLastCheckpoint(fs, expect_cp_position, after_mkfs, &cp_page);
  Checkpoint *cp = static_cast<Checkpoint *>(PageAddress(cp_page));
  ASSERT_EQ(cp->checkpoint_ver, expect_cp_ver);

  uint32_t orphan_inos = kOrphansPerBlock * kOrphanInodeBlockCnt;
  uint32_t start_ino = (cp->checkpoint_ver - 1) * orphan_inos;

  fs->InitOrphanInfo();

  if (!after_mkfs) {
    // 2. Get orphan inodes
    std::vector<uint32_t> cp_inos;
    std::vector<uint32_t> exp_inos(orphan_inos);
    std::iota(exp_inos.begin(), exp_inos.end(), start_ino);

    block_t start_blk = cp_page->index + 1;
    block_t orphan_blkaddr = cp->cp_pack_start_sum - 1;

    ASSERT_EQ(cp->ckpt_flags & kCpOrphanPresentFlag, kCpOrphanPresentFlag);

    for (block_t i = 0; i < orphan_blkaddr; i++) {
      Page *page = fs->GetMetaPage(start_blk + i);
      OrphanBlock *orphan_blk;

      orphan_blk = static_cast<OrphanBlock *>(PageAddress(page));
      for (block_t j = 0; j < LeToCpu(orphan_blk->entry_count); j++) {
        nid_t ino = LeToCpu(orphan_blk->ino[j]);
        cp_inos.push_back(ino);
      }
      F2fsPutPage(page, 1);
    }

    // 3. Check orphan inodes
    ASSERT_TRUE(std::equal(exp_inos.begin(), exp_inos.end(), cp_inos.begin()));
  }

  // 4. Add shuffled orphan inodes for next checkpoint
  std::vector<uint32_t> inos(orphan_inos);
  start_ino = cp->checkpoint_ver * orphan_inos;
  std::iota(inos.begin(), inos.end(), start_ino);

  uint64_t seed = cp->checkpoint_ver;
  std::shuffle(inos.begin(), inos.end(), std::default_random_engine(seed));

  for (auto ino : inos) {
    fs->AddOrphanInode(ino);
  }

  ASSERT_EQ(fs->GetSbInfo().n_orphans, orphan_inos);

  // Add duplicate orphan inodes
  std::vector<uint32_t> dup_inos(orphan_inos / 10);
  std::generate(dup_inos.begin(), dup_inos.end(), [n = start_ino]() mutable {
    n += 10;
    return n - 10;
  });

  for (auto ino : dup_inos) {
    fs->AddOrphanInode(ino);
  }

  F2fsPutPage(cp_page, 1);
}

void CheckpointTestRemoveOrphanInode(F2fs *fs, uint32_t expect_cp_position, uint32_t expect_cp_ver,
                                     bool after_mkfs) {
  Page *cp_page = nullptr;

  // 1. Get last checkpoint
  GetLastCheckpoint(fs, expect_cp_position, after_mkfs, &cp_page);
  Checkpoint *cp = static_cast<Checkpoint *>(PageAddress(cp_page));
  ASSERT_EQ(cp->checkpoint_ver, expect_cp_ver);

  uint32_t orphan_inos = kOrphansPerBlock * kOrphanInodeBlockCnt;
  uint32_t start_ino = (cp->checkpoint_ver - 1) * orphan_inos;

  fs->InitOrphanInfo();

  if (!after_mkfs) {
    // 2. Get orphan inodes
    std::vector<uint32_t> cp_inos;
    std::vector<uint32_t> exp_inos(orphan_inos);
    std::iota(exp_inos.begin(), exp_inos.end(), start_ino);

    // Remove exp orphan inodes
    for (int i = orphan_inos / 10 - 1; i >= 0; i--) {
      exp_inos.erase(exp_inos.begin() + (i * 10));
    }

    block_t start_blk = cp_page->index + 1;
    block_t orphan_blkaddr = cp->cp_pack_start_sum - 1;

    ASSERT_EQ(cp->ckpt_flags & kCpOrphanPresentFlag, kCpOrphanPresentFlag);

    for (block_t i = 0; i < orphan_blkaddr; i++) {
      Page *page = fs->GetMetaPage(start_blk + i);
      OrphanBlock *orphan_blk;

      orphan_blk = static_cast<OrphanBlock *>(PageAddress(page));
      for (block_t j = 0; j < LeToCpu(orphan_blk->entry_count); j++) {
        nid_t ino = LeToCpu(orphan_blk->ino[j]);
        cp_inos.push_back(ino);
      }
      F2fsPutPage(page, 1);
    }

    // 3. Check orphan inodes
    ASSERT_TRUE(std::equal(exp_inos.begin(), exp_inos.end(), cp_inos.begin()));
  }

  // 4. Add shuffled orphan inodes for next checkpoint
  std::vector<uint32_t> inos(orphan_inos);
  start_ino = cp->checkpoint_ver * orphan_inos;
  std::iota(inos.begin(), inos.end(), start_ino);

  uint64_t seed = cp->checkpoint_ver;
  std::shuffle(inos.begin(), inos.end(), std::default_random_engine(seed));

  for (auto ino : inos) {
    fs->AddOrphanInode(ino);
  }

  ASSERT_EQ(fs->GetSbInfo().n_orphans, orphan_inos);

  // 5. Remove orphan inodes
  std::vector<uint32_t> rm_inos(orphan_inos / 10);
  std::generate(rm_inos.begin(), rm_inos.end(), [n = start_ino]() mutable {
    n += 10;
    return n - 10;
  });

  for (auto ino : rm_inos) {
    fs->RemoveOrphanInode(ino);
  }

  F2fsPutPage(cp_page, 1);
}

void CheckpointTestRecoverOrphanInode(F2fs *fs, uint32_t expect_cp_position, uint32_t expect_cp_ver,
                                      bool after_mkfs,
                                      std::vector<fbl::RefPtr<VnodeF2fs>> &vnodes) {
  Page *cp_page = nullptr;

  // 1. Get last checkpoint
  GetLastCheckpoint(fs, expect_cp_position, after_mkfs, &cp_page);
  Checkpoint *cp = static_cast<Checkpoint *>(PageAddress(cp_page));
  ASSERT_EQ(cp->checkpoint_ver, expect_cp_ver);

  uint32_t orphan_inos = kOrphansPerBlock;
  uint32_t start_ino = (cp->checkpoint_ver - 1) * orphan_inos;

  fs->InitOrphanInfo();

  if (!after_mkfs) {
    // 2. Check recovery orphan inodes
    ASSERT_EQ(cp->ckpt_flags & kCpOrphanPresentFlag, kCpOrphanPresentFlag);
    ASSERT_EQ(vnodes.size(), orphan_inos);

    for (auto &vnode_refptr : vnodes) {
      ASSERT_EQ(vnode_refptr.get()->GetNlink(), (uint32_t)1);
    }

    ASSERT_EQ(fs->RecoverOrphanInodes(), 0);

    // TODO(jaeyoon): Add vnode null check when Iput() is enabled.
    for (auto &vnode_refptr : vnodes) {
      ASSERT_EQ(vnode_refptr.get()->GetNlink(), (uint32_t)0);
      vnode_refptr.reset();
    }
    vnodes.clear();
    vnodes.shrink_to_fit();
  }

  if (cp->checkpoint_ver > kCheckpointLoopCnt)
    return;

  // 3. Add shuffled orphan inodes for next checkpoint
  std::vector<uint32_t> inos(orphan_inos);
  start_ino = cp->checkpoint_ver * orphan_inos;
  std::iota(inos.begin(), inos.end(), start_ino);

  uint64_t seed = cp->checkpoint_ver;
  std::shuffle(inos.begin(), inos.end(), std::default_random_engine(seed));

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
    fs->AddOrphanInode(ino);
    vnode_refptr.reset();
  }

  ASSERT_EQ(fs->GetSbInfo().n_orphans, orphan_inos);

  F2fsPutPage(cp_page, 1);
}

void CheckpointTestCompactedSummaries(F2fs *fs, uint32_t expect_cp_position, uint32_t expect_cp_ver,
                                      bool after_mkfs) {
  SbInfo &sbi = fs->GetSbInfo();
  Page *cp_page = nullptr;

  // 1. Get last checkpoint
  GetLastCheckpoint(fs, expect_cp_position, after_mkfs, &cp_page);
  Checkpoint *cp = static_cast<Checkpoint *>(PageAddress(cp_page));
  ASSERT_EQ(cp->checkpoint_ver, expect_cp_ver);

  if (!after_mkfs) {
    // 2. Clear current segment summaries
    for (int i = static_cast<int>(CursegType::kCursegHotData);
         i <= static_cast<int>(CursegType::kCursegColdData); i++) {
      CursegInfo *curseg = SegMgr::CURSEG_I(&sbi, static_cast<CursegType>(i));
      for (auto &entrie : curseg->sum_blk->entries) {
        entrie.nid = 0;
        entrie.version = 0;
        entrie.ofs_in_node = 0;
      }
    }

    // 3. Recover compacted data summaries
    ASSERT_EQ(cp->ckpt_flags & kCpCompactSumFlag, kCpCompactSumFlag);
    ASSERT_EQ(fs->Segmgr().ReadCompactedSummaries(), 0);

    // 4. Check recovered active summary info
    for (int i = static_cast<int>(CursegType::kCursegHotData);
         i <= static_cast<int>(CursegType::kCursegColdData); i++) {
      CursegInfo *curseg = SegMgr::CURSEG_I(&sbi, static_cast<CursegType>(i));

      if (cp->checkpoint_ver > 3)  // cp_ver 2 and 3 have random segno
        ASSERT_EQ(curseg->segno, (cp->checkpoint_ver - 3) * 3 + i + 1);
      ASSERT_EQ(curseg->next_blkoff, 256);

      for (uint32_t j = 0; j < kEntriesInSum / 2; j++) {
        if (cp->checkpoint_ver == 2 &&
            IsRootInode(static_cast<CursegType>(i), j))  // root inode dentry
          continue;

        ASSERT_EQ(curseg->sum_blk->entries[j].nid, (uint32_t)3);
        ASSERT_EQ(static_cast<uint64_t>(curseg->sum_blk->entries[j].version),
                  cp->checkpoint_ver - 1);
        ASSERT_EQ(curseg->sum_blk->entries[j].ofs_in_node, j);
      }
    }
  }

  // 5. Fill compact data summary
  // Close and change current active segment
  // Fill current active segments for compacted data summaries
  for (int i = static_cast<int>(CursegType::kCursegHotData);
       i <= static_cast<int>(CursegType::kCursegColdData); i++) {
    // Close previous segment
    if (!after_mkfs) {
      for (uint32_t j = 0; j < kEntriesInSum / 2; j++) {
        block_t new_blkaddr;
        DoWriteSit(fs, &new_blkaddr, static_cast<CursegType>(i), kNullSegNo);
      }
    }

    // Write workload
    for (uint32_t j = 0; j < kEntriesInSum / 2; j++) {
      block_t new_blkaddr;
      Summary sum;

      if (cp->checkpoint_ver == 1 &&
          IsRootInode(static_cast<CursegType>(i), j))  // root inode dentry
        continue;

      fs->Segmgr().SetSummary(&sum, 3, j, cp->checkpoint_ver);
      fs->Segmgr().AddSumEntry(static_cast<CursegType>(i), &sum, j);

      DoWriteSit(fs, &new_blkaddr, static_cast<CursegType>(i), kNullSegNo);
    }
  }
  ASSERT_LT(fs->Segmgr().NpagesForSummaryFlush(), 3);

  F2fsPutPage(cp_page, 1);
}

void CheckpointTestNormalSummaries(F2fs *fs, uint32_t expect_cp_position, uint32_t expect_cp_ver,
                                   bool after_mkfs) {
  SbInfo &sbi = fs->GetSbInfo();
  Page *cp_page = nullptr;

  // 1. Get last checkpoint
  GetLastCheckpoint(fs, expect_cp_position, after_mkfs, &cp_page);
  Checkpoint *cp = static_cast<Checkpoint *>(PageAddress(cp_page));
  ASSERT_EQ(cp->checkpoint_ver, expect_cp_ver);

  if (!after_mkfs) {
    // 2. Clear current segment summaries
    for (int i = static_cast<int>(CursegType::kCursegHotData);
         i <= static_cast<int>(CursegType::kCursegColdNode); i++) {
      CursegInfo *curseg = SegMgr::CURSEG_I(&sbi, static_cast<CursegType>(i));
      for (auto &entrie : curseg->sum_blk->entries) {
        entrie.nid = 0;
        entrie.version = 0;
        entrie.ofs_in_node = 0;
      }
    }

    // 2. Recover normal data summary
    ASSERT_NE(cp->ckpt_flags & kCpCompactSumFlag, kCpCompactSumFlag);
    for (int type = static_cast<int>(CursegType::kCursegHotData);
         type <= static_cast<int>(CursegType::kCursegColdNode); type++) {
      ASSERT_EQ(fs->Segmgr().ReadNormalSummaries(type), ZX_OK);
    }

    // 4. Check recovered active summary info
    for (int i = static_cast<int>(CursegType::kCursegHotData);
         i <= static_cast<int>(CursegType::kCursegColdNode); i++) {
      CursegInfo *curseg = SegMgr::CURSEG_I(&sbi, static_cast<CursegType>(i));

      if (cp->checkpoint_ver > 3)  // cp_ver 2 and 3 have random segno
        ASSERT_EQ(curseg->segno, (cp->checkpoint_ver - 3) * 6 + i + 1);
      ASSERT_EQ(curseg->next_blkoff, 512);

      for (uint32_t j = 0; j < kEntriesInSum; j++) {
        if (cp->checkpoint_ver == 2 && IsRootInode(static_cast<CursegType>(i), j))  // root inode
          continue;

        ASSERT_EQ(curseg->sum_blk->entries[j].nid, cp->checkpoint_ver - 1);
        if (!IsNodeSeg(static_cast<CursegType>(i))) {
          ASSERT_EQ(static_cast<uint64_t>(curseg->sum_blk->entries[j].version),
                    cp->checkpoint_ver - 1);
          ASSERT_EQ(curseg->sum_blk->entries[j].ofs_in_node, j);
        }
      }
    }
  }

  // 3. Fill normal data summary
  // Close and change current active segment
  // Fill current active segments for normal summaries
  for (int i = static_cast<int>(CursegType::kCursegHotData);
       i <= static_cast<int>(CursegType::kCursegColdNode); i++) {
    for (uint32_t j = 0; j < kEntriesInSum; j++) {
      block_t new_blkaddr;
      Summary sum;

      if (cp->checkpoint_ver == 1 && IsRootInode(static_cast<CursegType>(i), j))  // root inode
        continue;

      fs->Segmgr().SetSummary(&sum, cp->checkpoint_ver, j, cp->checkpoint_ver);
      fs->Segmgr().AddSumEntry(static_cast<CursegType>(i), &sum, j);

      DoWriteSit(fs, &new_blkaddr, static_cast<CursegType>(i), kNullSegNo);
    }
  }
  ASSERT_GT(fs->Segmgr().NpagesForSummaryFlush(), 2);

  F2fsPutPage(cp_page, 1);
}

void CheckpointTestSitJournal(F2fs *fs, uint32_t expect_cp_position, uint32_t expect_cp_ver,
                              bool after_mkfs, std::vector<uint32_t> &segnos) {
  SbInfo &sbi = fs->GetSbInfo();
  Page *cp_page = nullptr;

  // 1. Get last checkpoint
  GetLastCheckpoint(fs, expect_cp_position, after_mkfs, &cp_page);
  Checkpoint *cp = static_cast<Checkpoint *>(PageAddress(cp_page));
  ASSERT_EQ(cp->checkpoint_ver, expect_cp_ver);

  if (!after_mkfs) {
    // 2. Recover compacted data summaries
    ASSERT_EQ(cp->ckpt_flags & kCpCompactSumFlag, kCpCompactSumFlag);
    ASSERT_EQ(fs->Segmgr().ReadCompactedSummaries(), 0);

    // 3. Check recovered journal
    CursegInfo *curseg = SegMgr::CURSEG_I(&sbi, CursegType::kCursegColdData);

#ifdef F2FS_BU_DEBUG
    std::cout << "Check Journal, CP ver =" << cp->checkpoint_ver
              << ", SitsInCursum=" << SitsInCursum(curseg->sum_blk)
              << ", dirty_sentries=" << GetSitInfo(&sbi)->dirty_sentries << std::endl;
#endif
    SummaryBlock *sum = curseg->sum_blk;
    for (int i = 0; i < SitsInCursum(sum); i++) {
      uint32_t segno = LeToCpu(SegnoInJournal(sum, i));
      ASSERT_EQ(segno, segnos[i]);
    }
  }

  // 4. Fill compact data summary
  if (!after_mkfs) {
    CursegInfo *curseg = SegMgr::CURSEG_I(&sbi, CursegType::kCursegColdData);

    // Clear SIT journal
    if (SitsInCursum(curseg->sum_blk) >= static_cast<int>(kSitJournalEntries)) {
      SitInfo *sit_i = GetSitInfo(&sbi);
      uint64_t *bitmap = sit_i->dirty_sentries_bitmap;
      uint64_t nsegs = TotalSegs(&sbi);
      uint32_t segno = -1;

      // Add dummy dirty sentries
      for (uint32_t i = 0; i < kMapPerSitEntry; i++) {
        block_t new_blkaddr;
        DoWriteSit(fs, &new_blkaddr, CursegType::kCursegColdData, kNullSegNo);
      }

      // Move journal sentries to dirty sentries
      ASSERT_TRUE(fs->Segmgr().FlushSitsInJournal());

      // Clear dirty sentries
      while ((segno = find_next_bit_le(bitmap, nsegs, segno + 1)) < nsegs) {
        __clear_bit(segno, bitmap);
        sit_i->dirty_sentries--;
      }

#ifdef F2FS_BU_DEBUG
      std::cout << "Clear Journal CP ver =" << cp->checkpoint_ver
                << ", SitsInCursum=" << SitsInCursum(curseg->sum_blk)
                << ", dirty_sentries=" << GetSitInfo(&sbi)->dirty_sentries << std::endl;
#endif
    }
  }
  segnos.clear();
  segnos.shrink_to_fit();

  // Fill SIT journal
  for (uint32_t i = 0; i < kSitJournalEntries * kMapPerSitEntry; i++) {
    block_t new_blkaddr;
    DoWriteSit(fs, &new_blkaddr, CursegType::kCursegColdData, kNullSegNo);
    CursegInfo *curseg = SegMgr::CURSEG_I(&sbi, CursegType::kCursegColdData);
    if (curseg->next_blkoff == 1) {
      segnos.push_back(curseg->segno);
    }
  }
  ASSERT_LT(fs->Segmgr().NpagesForSummaryFlush(), 3);

  F2fsPutPage(cp_page, 1);
}

void CheckpointTestNatJournal(F2fs *fs, uint32_t expect_cp_position, uint32_t expect_cp_ver,
                              bool after_mkfs, std::vector<uint32_t> &nids) {
  SbInfo &sbi = fs->GetSbInfo();
  NmInfo *nm_i = GetNmInfo(&sbi);
  Page *cp_page = nullptr;
  CursegInfo *curseg = SegMgr::CURSEG_I(&sbi, CursegType::kCursegHotData);

  // 1. Get last checkpoint
  GetLastCheckpoint(fs, expect_cp_position, after_mkfs, &cp_page);
  Checkpoint *cp = static_cast<Checkpoint *>(PageAddress(cp_page));
  ASSERT_EQ(cp->checkpoint_ver, expect_cp_ver);

  if (!after_mkfs) {
    // 2. Recover compacted data summaries
    ASSERT_EQ(cp->ckpt_flags & kCpCompactSumFlag, kCpCompactSumFlag);
    ASSERT_EQ(fs->Segmgr().ReadCompactedSummaries(), 0);

    // 3. Check recovered journal
#ifdef F2FS_BU_DEBUG
    std::cout << "Check Journal, CP ver =" << cp->checkpoint_ver
              << ", NatsInCursum=" << NatsInCursum(curseg->sum_blk)
              << ", dirty_nat_cnt=" << list_length(&nm_i->dirty_nat_entries) << std::endl;
#endif
    SummaryBlock *sum = curseg->sum_blk;
    for (int i = 0; i < NatsInCursum(sum); i++) {
      ASSERT_EQ(NidInJournal(sum, i), nids[i]);
      ASSERT_EQ(NatInJournal(sum, i).version, cp->checkpoint_ver - 1);
    }
  }

  // 4. Fill compact data summary
  if (!after_mkfs) {
    // Clear NAT journal
    if (NatsInCursum(curseg->sum_blk) >= static_cast<int>(kNatJournalEntries)) {
      // Add dummy dirty NAT entries
      DoWriteNat(fs, kNatJournalEntries, kNatJournalEntries, cp->checkpoint_ver);

      // Move journal sentries to dirty sentries
      ASSERT_TRUE(fs->Nodemgr().FlushNatsInJournal());

      // Clear dirty sentries
      list_node_t *cur, *n;
      list_for_every_safe(&nm_i->dirty_nat_entries, cur, n) {
        NatEntry *ne = containerof(cur, NatEntry, list);
        list_delete(&ne->list);
        nm_i->nat_cnt--;
        delete ne;
        ne->checkpointed = true;
      }

#ifdef F2FS_BU_DEBUG
      std::cout << "Clear Journal, CP ver =" << cp->checkpoint_ver
                << ", NatsInCursum=" << NatsInCursum(curseg->sum_blk)
                << ", dirty_nat_cnt=" << list_length(&nm_i->dirty_nat_entries) << std::endl;
#endif
    }
  }
  nids.clear();
  nids.shrink_to_fit();

  // Fill NAT journal
  for (uint32_t i = 0; i < kNatJournalEntries; i++) {
    DoWriteNat(fs, i, i, cp->checkpoint_ver);
    nids.push_back(i);
  }
  ASSERT_LT(fs->Segmgr().NpagesForSummaryFlush(), 3);

  F2fsPutPage(cp_page, 1);
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

  unittest_lib::MkfsOnFakeDev(&bc, kBlockCount);
  unittest_lib::MountWithOptions(options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  unittest_lib::CreateRoot(fs.get(), &root);

  // Validate checkpoint
  for (uint32_t i = 1; i <= kCheckpointLoopCnt + 1; i++) {
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

  unittest_lib::Unmount(std::move(fs), &bc);

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

}  // namespace
}  // namespace f2fs
