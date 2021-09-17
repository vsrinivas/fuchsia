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
  node_mgr_->DestroyNodeManager();
  seg_mgr_->DestroySegmentManager();

  delete reinterpret_cast<FsBlock *>(sbi_->ckpt);

#if 0  // porting needed
  //   brelse(sbi_->raw_super_buf);
#endif
  node_mgr_.reset();
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

int F2fs::SanityCheckCkpt() {
  unsigned int total, fsmeta;

  total = LeToCpu(raw_sb_->segment_count);
  fsmeta = LeToCpu(raw_sb_->segment_count_ckpt);
  fsmeta += LeToCpu(raw_sb_->segment_count_sit);
  fsmeta += LeToCpu(raw_sb_->segment_count_nat);
  fsmeta += LeToCpu(sbi_->ckpt->rsvd_segment_count);
  fsmeta += LeToCpu(raw_sb_->segment_count_ssa);

  if (fsmeta >= total)
    return 1;
  return 0;
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

zx_status_t F2fs::FillSuper() {
#if 0  // porting needed
  //   SuperBlock *raw_super;
  //   buffer_head *raw_super_buf = NULL;
#endif
  VnodeF2fs *root;
  zx_status_t err = ZX_ERR_INVALID_ARGS;

  /* allocate memory for f2fs-specific super block info */
  sbi_ = std::make_unique<SbInfo>();
  if (!sbi_)
    return ZX_ERR_NO_MEMORY;
  memset(sbi_.get(), 0, sizeof(SbInfo));

#if 0  // porting needed
  // super_block *sb = sbi_->sb;

  /* set a temporary block size */
  // if (!SbSetBlocksize(sb, kBlockSize))
  //  goto free_sbi;
#endif

  ParseOptions();

  // sanity checking of raw super
  if (SanityCheckRawSuper() != ZX_OK) {
    goto free_sb_buf;
  }

#if 0  // porting needed
  // sb->s_maxbytes = MaxFileSize(RawSb().log_blocksize);
  // sb->s_max_links = kLinkMax;

  // For NFS support
  // get_random_bytes(&sbi->s_next_generation, sizeof(uint32_t));

  // sb->s_op = &f2fs_sops;
  //  sb->s_xattr = f2fs_xattr_handlers;
  //  sb->s_export_op = &f2fs_export_ops;
  // sb->s_magic = kF2fsSuperMagic;
  // sb->s_fs_info = sbi_.get();
  // sb->s_time_gran = 1;
  // sb->s_flags = (sb->s_flags & ~MS_POSIXACL) |
  //  (TestOpt(sbi_.get(), kMountPosixAcl) ? MS_POSIXACL : 0);
  // memcpy(&sb->s_uuid, RawSb().uuid, sizeof(RawSb().uuid));

  /* init f2fs-specific super block info */
  //   sbi->sb = sb;
#endif
  sbi_->raw_super = raw_sb_.get();
#if 0  // porting needed
  //   sbi_->raw_super_buf = raw_super_buf;
#endif
  sbi_->por_doing = 0;
#if 0  // porting needed
  // init_rwsem(&sbi->bio_sem);
#endif
  InitSbInfo();

  /* get an inode for meta space */
#if 0  // porting needed
  // err = VnodeF2fs::Vget(this, MetaIno(sbi_), &sbi_->meta_vnode);
  // if (err) {
  //   goto free_sb_buf;
  // }
#endif

  err = GetValidCheckpoint();
  if (err)
    goto free_meta_inode;

  /* sanity checking of checkpoint */
  err = ZX_ERR_INVALID_ARGS;
  if (SanityCheckCkpt())
    goto free_cp;

  sbi_->total_valid_node_count = LeToCpu(sbi_->ckpt->valid_node_count);
  sbi_->total_valid_inode_count = LeToCpu(sbi_->ckpt->valid_inode_count);
  sbi_->user_block_count = static_cast<block_t>(LeToCpu(sbi_->ckpt->user_block_count));
  sbi_->total_valid_block_count = static_cast<block_t>(LeToCpu(sbi_->ckpt->valid_block_count));
  sbi_->last_valid_block_count = sbi_->total_valid_block_count;
  sbi_->alloc_valid_block_count = 0;
  list_initialize(&sbi_->dir_inode_list);

  /* init super block */
#if 0  // porting needed
  // if (!SbSetBlocksize(sb, sbi_->blocksize))
  //  goto free_cp;
#endif

  InitOrphanInfo();

  /* setup f2fs internal modules */
  seg_mgr_ = std::make_unique<SegMgr>(this);
  err = seg_mgr_->BuildSegmentManager();
  if (err)
    goto free_sm;

  node_mgr_ = std::make_unique<NodeManager>(this);
  err = node_mgr_->BuildNodeManager();
  if (err)
    goto free_nm;

#if 0  // porting needed
  // BuildGcManager(sbi);

  /* get an inode for node space */
  // err = VnodeF2fs::Vget(this, NodeIno(sbi_), &sbi_->node_vnode);
  // if (err) {
  //   goto free_nm;
  // }
#endif

  /* if there are nt orphan nodes free them */
  err = ZX_ERR_INVALID_ARGS;
  if (RecoverOrphanInodes() != ZX_OK)
    goto free_node_inode;

  /* read root inode and dentry */
  err = VnodeF2fs::Vget(this, RootIno(sbi_.get()), &root_vnode_);
  if (err) {
    goto free_node_inode;
  }
  root = root_vnode_.get();
  if (!root->IsDir() || !root->GetBlocks() || !root->GetSize())
    goto free_root_inode;

#if 0  // porting needed
  // sb->s_root = DMakeRoot(root); /* allocate root dentry */
  // if (!sb->s_root) {
  //   err = ZX_ERR_NO_MEMORY;
  //   goto free_root_inode;
  // }
#endif

  // try to recover fsynced data every mount time
  if (!TestOpt(sbi_.get(), kMountDisableRollForward)) {
    RecoverFsyncData();
  }

#if 0  // porting needed
  /* After POR, we can run background GC thread */
  // err = StartGcThread(sbi);
  // if (err)
  //   goto fail;

  // err = BuildStats(sbi_.get());
  // if (err)
  //  goto fail;
#endif

  return ZX_OK;

#if 0  // porting needed
fail:
  // StopGcThread(sbi);
#endif
free_root_inode:
#if 0  // porting needed
//  dput(sb->s_root);
//  sb->s_root = NULL;
#endif
free_node_inode:
#if 0  // porting needed
  // Iput(sbi_->node_inode);
#endif
free_nm:
  node_mgr_->DestroyNodeManager();
  node_mgr_.reset();
free_sm:
  seg_mgr_->DestroySegmentManager();
  seg_mgr_.reset();
free_cp:
  delete reinterpret_cast<FsBlock *>(sbi_->ckpt);
free_meta_inode:
#if 0  // porting needed
  // MakeBadInode(sbi_->meta_inode);
  // Iput(sbi_->meta_inode);
#endif
free_sb_buf:
#if 0  // porting needed
  //   brelse(raw_super_buf);
  // free_sbi:
#endif
  sbi_.reset();
  return err;
}

}  // namespace f2fs
