// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

void F2fs::PutSuper() {
#if 0  // porting needed
  // DestroyStats(sbi_.get());
  // StopGcThread(sbi_.get());
#endif

  WriteCheckpoint(false, true);
  GetVCache().Reset();

#if 0  // porting needed
  // Iput(sbi_->node_inode);
  // Iput(sbi_->meta_inode);
#endif

  /* destroy f2fs internal modules */
  node_manager_->DestroyNodeManager();
  seg_mgr_->DestroySegmentManager();

  delete reinterpret_cast<FsBlock *>(sbi_->ckpt);

#if 0  // porting needed
  //   brelse(sbi_->raw_super_buf);
#endif
  node_manager_.reset();
  seg_mgr_.reset();
  raw_sb_.reset();
  sbi_.reset();
}

zx_status_t F2fs::SyncFs(int sync) {
#ifdef F2FS_BU_DEBUG
  FX_LOGS(DEBUG) << "F2fs::SyncFs, sbi_->s_dirty=" << sbi_->s_dirty;
#endif

#if 0  // porting needed
  //if (!sbi_->s_dirty && !GetPages(sbi_.get(), CountType::kDirtyNodes))
  //  return 0;
#endif

  if (sync)
    WriteCheckpoint(false, false);

  return ZX_OK;
}

#if 0  // porting needed
// int F2fs::F2fsStatfs(dentry *dentry /*, kstatfs *buf*/) {
  // super_block *sb = dentry->d_sb;
  // SbInfo *sbi = F2FS_SB(sb);
  // u64 id = huge_encode_dev(sb->s_bdev->bd_dev);
  // block_t total_count, user_block_count, start_count, ovp_count;

  // total_count = LeToCpu(sbi->raw_super->block_count);
  // user_block_count = sbi->user_block_count;
  // start_count = LeToCpu(sbi->raw_super->segment0_blkaddr);
  // ovp_count = GetSmInfo(sbi)->ovp_segments << sbi->log_blocks_per_seg;
  // buf->f_type = kF2fsSuperMagic;
  // buf->f_bsize = sbi->blocksize;

  // buf->f_blocks = total_count - start_count;
  // buf->f_bfree = buf->f_blocks - ValidUserBlocks(sbi) - ovp_count;
  // buf->f_bavail = user_block_count - ValidUserBlocks(sbi);

  // buf->f_files = ValidInodeCount(sbi);
  // buf->f_ffree = sbi->total_node_count - ValidNodeCount(sbi);

  // buf->f_namelen = kMaxNameLen;
  // buf->f_fsid.val[0] = (u32)id;
  // buf->f_fsid.val[1] = (u32)(id >> 32);

  // return 0;
// }

// VnodeF2fs *F2fs::F2fsNfsGetInode(uint64_t ino, uint32_t generation) {
//   fbl::RefPtr<VnodeF2fs> vnode_refptr;
//   VnodeF2fs *vnode = nullptr;
//   int err;

//   if (ino < RootIno(sbi_.get()))
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
          sbi_->active_logs = value;
          break;
        case kOptDiscard:
          if (value)
            SetOpt(sbi_.get(), kMountDiscard);
          break;
        case kOptBgGcOff:
          if (value)
            SetOpt(sbi_.get(), kMountBgGcOff);
          break;
        case kOptNoHeap:
          if (value)
            SetOpt(sbi_.get(), kMountNoheap);
          break;
        case kOptDisableExtIdentify:
          if (value)
            SetOpt(sbi_.get(), kMountDisableExtIdentify);
          break;
        case kOptNoUserXAttr:
          if (value)
            SetOpt(sbi_.get(), kMountNoXAttr);
          break;
        case kOptNoAcl:
          if (value)
            SetOpt(sbi_.get(), kMountNoAcl);
          break;
        case kOptDisableRollForward:
          if (value)
            SetOpt(sbi_.get(), kMountDisableRollForward);
          break;
        case kOptInlineXattr:
          if (value)
            SetOpt(sbi_.get(), kMountInlineXattr);
          break;
        case kOptInlineData:
          if (value)
            SetOpt(sbi_.get(), kMountInlineData);
          break;
        case kOptInlineDentry:
          if (value)
            SetOpt(sbi_.get(), kMountInlineDentry);
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
  fsmeta += LeToCpu(sbi_->ckpt->rsvd_segment_count);
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

  if (sbi_->ckpt->sit_ver_bitmap_bytesize != sit_ver_bitmap_bytesize ||
      sbi_->ckpt->nat_ver_bitmap_bytesize != nat_ver_bitmap_bytesize ||
      sbi_->ckpt->next_free_nid >= kNatEntryPerBlock * nat_blocks) {
    return ZX_ERR_BAD_STATE;
  }

  return ZX_OK;
}

void F2fs::InitSbInfo() {
  int i;

  sbi_->log_sectors_per_block = LeToCpu(RawSb().log_sectors_per_block);
  sbi_->log_blocksize = LeToCpu(RawSb().log_blocksize);
  sbi_->blocksize = 1 << sbi_->log_blocksize;
  sbi_->log_blocks_per_seg = LeToCpu(RawSb().log_blocks_per_seg);
  sbi_->blocks_per_seg = 1 << sbi_->log_blocks_per_seg;
  sbi_->segs_per_sec = LeToCpu(RawSb().segs_per_sec);
  sbi_->secs_per_zone = LeToCpu(RawSb().secs_per_zone);
  sbi_->total_sections = LeToCpu(RawSb().section_count);
  sbi_->total_node_count =
      (LeToCpu(RawSb().segment_count_nat) / 2) * sbi_->blocks_per_seg * kNatEntryPerBlock;
  sbi_->root_ino_num = LeToCpu(RawSb().root_ino);
  sbi_->node_ino_num = LeToCpu(RawSb().node_ino);
  sbi_->meta_ino_num = LeToCpu(RawSb().meta_ino);

  for (i = 0; i < static_cast<int>(CountType::kNrCountType); i++)
    AtomicSet(&sbi_->nr_pages[i], 0);
}

void F2fs::Reset() {
  if (root_vnode_) {
    root_vnode_.reset();
  }
  if (node_manager_) {
    node_manager_->DestroyNodeManager();
    node_manager_.reset();
  }
  if (seg_mgr_) {
    seg_mgr_->DestroySegmentManager();
    seg_mgr_.reset();
  }
  if (sbi_->ckpt) {
    delete reinterpret_cast<FsBlock *>(sbi_->ckpt);
  }
  if (sbi_) {
    sbi_.reset();
  }
}

zx_status_t F2fs::FillSuper() {
  zx_status_t err = ZX_OK;
  auto reset = fit::defer([&] { Reset(); });

  // allocate memory for f2fs-specific super block info
  if (sbi_ = std::make_unique<SbInfo>(); sbi_ == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  memset(sbi_.get(), 0, sizeof(SbInfo));
  ParseOptions();

  // sanity checking of raw super
  if (err = SanityCheckRawSuper(); err != ZX_OK) {
    return err;
  }

  sbi_->raw_super = raw_sb_.get();
  sbi_->por_doing = 0;
  InitSbInfo();

  // TODO: Create node/meta vnode when impl dirty cache
  // if (err = VnodeF2fs::Vget(this, MetaIno(sbi_), &sbi_->meta_vnode); err) {
  //   goto free_sb_buf;
  // }

  if (err = GetValidCheckpoint(); err != ZX_OK) {
    return err;
  }

  // sanity checking of checkpoint
  if (err = SanityCheckCkpt(); err != ZX_OK) {
    return err;
  }

  sbi_->total_valid_node_count = LeToCpu(sbi_->ckpt->valid_node_count);
  sbi_->total_valid_inode_count = LeToCpu(sbi_->ckpt->valid_inode_count);
  sbi_->user_block_count = static_cast<block_t>(LeToCpu(sbi_->ckpt->user_block_count));
  sbi_->total_valid_block_count = static_cast<block_t>(LeToCpu(sbi_->ckpt->valid_block_count));
  sbi_->last_valid_block_count = sbi_->total_valid_block_count;
  sbi_->alloc_valid_block_count = 0;
  list_initialize(&sbi_->dir_inode_list);

  InitOrphanInfo();

  if (seg_mgr_ = std::make_unique<SegMgr>(this); seg_mgr_ == nullptr) {
    err = ZX_ERR_NO_MEMORY;
    return err;
  }

  if (err = seg_mgr_->BuildSegmentManager(); err != ZX_OK) {
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
  // BuildGcManager(sbi);

  // if there are nt orphan nodes free them
  if (err = RecoverOrphanInodes(); err != ZX_OK) {
    return err;
  }

  // read root inode and dentry
  if (err = VnodeF2fs::Vget(this, RootIno(sbi_.get()), &root_vnode_); err != ZX_OK) {
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
  if (!TestOpt(sbi_.get(), kMountDisableRollForward)) {
    RecoverFsyncData();
  }

  // After POR, we can run background GC thread
  // TODO: Enable wb thread first, and then impl gc thread
  // err = StartGcThread(sbi);
  // if (err)
  //   goto fail;
  reset.cancel();
  return err;
}

}  // namespace f2fs
