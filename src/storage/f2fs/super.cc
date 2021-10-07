// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <safemath/checked_math.h>

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

void F2fs::PutSuper() {
#if 0  // porting needed
  // DestroyStats(superblock_info_.get());
  // StopGcThread(superblock_info_.get());
#endif

  WriteCheckpoint(false, true);
  GetVCache().Reset();

#if 0  // porting needed
  // Iput(superblock_info_->node_inode);
  // Iput(superblock_info_->meta_inode);
#endif

  /* destroy f2fs internal modules */
  node_manager_->DestroyNodeManager();
  segment_manager_->DestroySegmentManager();

  node_manager_.reset();
  segment_manager_.reset();
  raw_sb_.reset();
  superblock_info_.reset();
}

zx_status_t F2fs::SyncFs(int sync) {
#ifdef F2FS_BU_DEBUG
  FX_LOGS(DEBUG) << "F2fs::SyncFs, superblock_info_.IsDirty()=" << superblock_info_.IsDirty();
#endif

#if 0  // porting needed
  //if (!superblock_info_.IsDirty() && !superblock_info_->GetPages(CountType::kDirtyNodes))
  //  return 0;
#endif

  if (sync)
    WriteCheckpoint(false, false);

  return ZX_OK;
}

#if 0  // porting needed
// int F2fs::F2fsStatfs(dentry *dentry /*, kstatfs *buf*/) {
  // super_block *sb = dentry->d_sb;
  // SuperblockInfo *superblock_info = F2FS_SB(sb);
  // u64 id = huge_encode_dev(sb->s_bdev->bd_dev);
  // block_t total_count, user_block_count, start_count, ovp_count;

  // total_count = LeToCpu(superblock_info->raw_super->block_count);
  // user_block_count = superblock_info->GetUserBlockCount();
  // start_count = LeToCpu(superblock_info->raw_super->segment0_blkaddr);
  // ovp_count = GetSmInfo(superblock_info).ovp_segments << superblock_info->GetLogBlocksPerSeg();
  // buf->f_type = kF2fsSuperMagic;
  // buf->f_bsize = superblock_info->GetBlocksize();

  // buf->f_blocks = total_count - start_count;
  // buf->f_bfree = buf->f_blocks - ValidUserBlocks(superblock_info) - ovp_count;
  // buf->f_bavail = user_block_count - ValidUserBlocks(superblock_info);

  // buf->f_files = ValidInodeCount(superblock_info);
  // buf->f_ffree = superblock_info->GetTotalNodeCount() - ValidNodeCount(superblock_info);

  // buf->f_namelen = kMaxNameLen;
  // buf->f_fsid.val[0] = (u32)id;
  // buf->f_fsid.val[1] = (u32)(id >> 32);

  // return 0;
// }

// VnodeF2fs *F2fs::F2fsNfsGetInode(uint64_t ino, uint32_t generation) {
//   fbl::RefPtr<VnodeF2fs> vnode_refptr;
//   VnodeF2fs *vnode = nullptr;
//   int err;

//   if (ino < superblock_info_->GetRootIno())
//     return (VnodeF2fs *)ErrPtr(-ESTALE);

//   /*
//    * f2fs_iget isn't quite right if the inode is currently unallocated!
//    * However f2fs_iget currently does appropriate checks to handle stale
//    * inodes so everything is OK.
//    */
//   err = VnodeF2fs::Vget(this, ino, &vnode_refptr);
//   if (err)
//     return (VnodeF2fs *)ErrPtr(err);
//   vnode = vnode_refptr.get();
//   if (generation && vnode->i_generation != generation) {
//     /* we didn't find the right inode.. */
//     Iput(vnode);
//     return (VnodeF2fs *)ErrPtr(-ESTALE);
//   }
//   return vnode;
// }

// struct fid {};

// dentry *F2fs::F2fsFhToDentry(fid *fid, int fh_len, int fh_type) {
//   return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
//             f2fs_nfs_get_inode);
// }

// dentry *F2fs::F2fsFhToParent(fid *fid, int fh_len, int fh_type) {
//   return generic_fh_to_parent(sb, fid, fh_len, fh_type,
//             f2fs_nfs_get_inode);
// }
#endif

void F2fs::ParseOptions() {
  for (uint32_t i = 0; i < kOptMaxNum; i++) {
    uint32_t value;
    if (mount_options_.GetValue(i, &value) == ZX_OK) {
      switch (i) {
        case kOptActiveLogs:
          superblock_info_->SetActiveLogs(value);
          break;
        case kOptDiscard:
          if (value)
            superblock_info_->SetOpt(kMountDiscard);
          break;
        case kOptBgGcOff:
          if (value)
            superblock_info_->SetOpt(kMountBgGcOff);
          break;
        case kOptNoHeap:
          if (value)
            superblock_info_->SetOpt(kMountNoheap);
          break;
        case kOptDisableExtIdentify:
          if (value)
            superblock_info_->SetOpt(kMountDisableExtIdentify);
          break;
        case kOptNoUserXAttr:
          if (value)
            superblock_info_->SetOpt(kMountNoXAttr);
          break;
        case kOptNoAcl:
          if (value)
            superblock_info_->SetOpt(kMountNoAcl);
          break;
        case kOptDisableRollForward:
          if (value)
            superblock_info_->SetOpt(kMountDisableRollForward);
          break;
        case kOptInlineXattr:
          if (value)
            superblock_info_->SetOpt(kMountInlineXattr);
          break;
        case kOptInlineData:
          if (value)
            superblock_info_->SetOpt(kMountInlineData);
          break;
        case kOptInlineDentry:
          if (value)
            superblock_info_->SetOpt(kMountInlineDentry);
          break;
        default:
          FX_LOGS(WARNING) << mount_options_.GetNameView(i) << " is not supported.";
          break;
      };
    }
  }
}

loff_t F2fs::MaxFileSize(unsigned bits) {
  loff_t result = kAddrsPerInode;
  loff_t leaf_count = kAddrsPerBlock;

  /* two direct node blocks */
  result += (leaf_count * 2);

  /* two indirect node blocks */
  leaf_count *= kNidsPerBlock;
  result += (leaf_count * 2);

  /* one double indirect node block */
  leaf_count *= kNidsPerBlock;
  result += leaf_count;

  result <<= bits;
  return result;
}

zx_status_t F2fs::SanityCheckRawSuper() {
  unsigned int blocksize;

  if (kF2fsSuperMagic != LeToCpu(raw_sb_->magic))
    return 1;

  // Currently, support 512/1024/2048/4096 block size
  blocksize = 1 << LeToCpu(raw_sb_->log_blocksize);
  if (blocksize != kPageCacheSize)
    return ZX_ERR_INVALID_ARGS;
  if (LeToCpu(raw_sb_->log_sectorsize) > kMaxLogSectorSize ||
      LeToCpu(raw_sb_->log_sectorsize) < kMinLogSectorSize)
    return ZX_ERR_INVALID_ARGS;
  if ((LeToCpu(raw_sb_->log_sectors_per_block) + LeToCpu(raw_sb_->log_sectorsize)) !=
      kMaxLogSectorSize)
    return ZX_ERR_INVALID_ARGS;
  return ZX_OK;
}

zx_status_t F2fs::SanityCheckCkpt() {
  unsigned int total, fsmeta;

  total = LeToCpu(raw_sb_->segment_count);
  fsmeta = LeToCpu(raw_sb_->segment_count_ckpt);
  fsmeta += LeToCpu(raw_sb_->segment_count_sit);
  fsmeta += LeToCpu(raw_sb_->segment_count_nat);
  fsmeta += LeToCpu(superblock_info_->GetCheckpoint().rsvd_segment_count);
  fsmeta += LeToCpu(raw_sb_->segment_count_ssa);

  if (fsmeta >= total) {
    return ZX_ERR_BAD_STATE;
  }

  uint32_t sit_ver_bitmap_bytesize =
      ((LeToCpu(raw_sb_->segment_count_sit) / 2) << LeToCpu(raw_sb_->log_blocks_per_seg)) / 8;
  uint32_t nat_ver_bitmap_bytesize =
      ((LeToCpu(raw_sb_->segment_count_nat) / 2) << LeToCpu(raw_sb_->log_blocks_per_seg)) / 8;
  block_t nat_blocks = (LeToCpu(raw_sb_->segment_count_nat) >> 1)
                       << LeToCpu(raw_sb_->log_blocks_per_seg);

  if (superblock_info_->GetCheckpoint().sit_ver_bitmap_bytesize != sit_ver_bitmap_bytesize ||
      superblock_info_->GetCheckpoint().nat_ver_bitmap_bytesize != nat_ver_bitmap_bytesize ||
      superblock_info_->GetCheckpoint().next_free_nid >= kNatEntryPerBlock * nat_blocks) {
    return ZX_ERR_BAD_STATE;
  }

  return ZX_OK;
}

void F2fs::InitSuperblockInfo() {
  int i;

  superblock_info_->SetLogSectorsPerBlock(LeToCpu(RawSb().log_sectors_per_block));
  superblock_info_->SetLogBlocksize(LeToCpu(RawSb().log_blocksize));
  superblock_info_->SetBlocksize(1 << superblock_info_->GetLogBlocksize());
  superblock_info_->SetLogBlocksPerSeg(LeToCpu(RawSb().log_blocks_per_seg));
  superblock_info_->SetBlocksPerSeg(1 << superblock_info_->GetLogBlocksPerSeg());
  superblock_info_->SetSegsPerSec(LeToCpu(RawSb().segs_per_sec));
  superblock_info_->SetSecsPerZone(LeToCpu(RawSb().secs_per_zone));
  superblock_info_->SetTotalSections(LeToCpu(RawSb().section_count));
  superblock_info_->SetTotalNodeCount((LeToCpu(RawSb().segment_count_nat) / 2) *
                                      superblock_info_->GetBlocksPerSeg() * kNatEntryPerBlock);
  superblock_info_->SetRootIno(LeToCpu(RawSb().root_ino));
  superblock_info_->SetNodeIno(LeToCpu(RawSb().node_ino));
  superblock_info_->SetMetaIno(LeToCpu(RawSb().meta_ino));

  for (i = 0; i < static_cast<int>(CountType::kNrCountType); i++)
    AtomicSet(&superblock_info_->GetNrPages(i), 0);
}

void F2fs::Reset() {
  if (root_vnode_) {
    root_vnode_.reset();
  }
  if (node_manager_) {
    node_manager_->DestroyNodeManager();
    node_manager_.reset();
  }
  if (segment_manager_) {
    segment_manager_->DestroySegmentManager();
    segment_manager_.reset();
  }
  if (superblock_info_) {
    superblock_info_.reset();
  }
}

zx_status_t F2fs::FillSuper() {
  zx_status_t err = ZX_OK;
  auto reset = fit::defer([&] { Reset(); });

  // allocate memory for f2fs-specific super block info
  if (superblock_info_ = std::make_unique<SuperblockInfo>(); superblock_info_ == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  ParseOptions();

  // sanity checking of raw super
  if (err = SanityCheckRawSuper(); err != ZX_OK) {
    return err;
  }

  superblock_info_->SetRawSuperblock(raw_sb_);
  superblock_info_->ClearOnRecovery();
  InitSuperblockInfo();

  // TODO: Create node/meta vnode when impl dirty cache
  // if (err = VnodeF2fs::Vget(this, superblock_info->GetMetaIno(), &superblock_info_->meta_vnode);
  // err) {
  //   goto free_sb_buf;
  // }

  if (err = GetValidCheckpoint(); err != ZX_OK) {
    return err;
  }

  // sanity checking of checkpoint
  if (err = SanityCheckCkpt(); err != ZX_OK) {
    return err;
  }

  superblock_info_->SetTotalValidNodeCount(
      LeToCpu(superblock_info_->GetCheckpoint().valid_node_count));
  superblock_info_->SetTotalValidInodeCount(
      LeToCpu(superblock_info_->GetCheckpoint().valid_inode_count));
  superblock_info_->SetUserBlockCount(
      static_cast<block_t>(LeToCpu(superblock_info_->GetCheckpoint().user_block_count)));
  superblock_info_->SetTotalValidBlockCount(
      static_cast<block_t>(LeToCpu(superblock_info_->GetCheckpoint().valid_block_count)));
  superblock_info_->SetLastValidBlockCount(superblock_info_->GetTotalValidBlockCount());
  superblock_info_->SetAllocValidBlockCount(0);
  list_initialize(&superblock_info_->GetDirInodeList());

  InitOrphanInfo();

  if (segment_manager_ = std::make_unique<SegmentManager>(this); segment_manager_ == nullptr) {
    err = ZX_ERR_NO_MEMORY;
    return err;
  }

  if (err = segment_manager_->BuildSegmentManager(); err != ZX_OK) {
    return err;
  }

  if (node_manager_ = std::make_unique<NodeManager>(this); node_manager_ == nullptr) {
    err = ZX_ERR_NO_MEMORY;
    return err;
  }

  if (err = node_manager_->BuildNodeManager(); err != ZX_OK) {
    return err;
  }

  // TODO: Enable gc after impl dirty data cache
  // BuildGcManager(superblock_info);

  // if there are nt orphan nodes free them
  if (err = RecoverOrphanInodes(); err != ZX_OK) {
    return err;
  }

  // read root inode and dentry
  if (err = VnodeF2fs::Vget(this, superblock_info_->GetRootIno(), &root_vnode_); err != ZX_OK) {
    err = ZX_ERR_NO_MEMORY;
    return err;
  }

  // root vnode is corrupted
  if (!root_vnode_->IsDir() || !root_vnode_->GetBlocks() || !root_vnode_->GetSize()) {
    return err;
  }

  // TODO: handling dentry cache
  // TODO: recover fsynced data every mount time
  // enable roll forward recovery when node dirty cache is impl.
  if (!superblock_info_->TestOpt(kMountDisableRollForward)) {
    RecoverFsyncData();
  }

  // After POR, we can run background GC thread
  // TODO: Enable wb thread first, and then impl gc thread
  // err = StartGcThread(superblock_info);
  // if (err)
  //   goto fail;
  reset.cancel();
  return err;
}

void SuperblockInfo::IncNrOrphans() { n_orphans_ = safemath::CheckAdd(n_orphans_, 1).ValueOrDie(); }

void SuperblockInfo::DecNrOrphans() { n_orphans_ = safemath::CheckSub(n_orphans_, 1).ValueOrDie(); }

}  // namespace f2fs
