// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

#include <fcntl.h>
#ifdef __Fuchsia__
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/trace-provider/provider.h>
#include <lib/zx/event.h>
#endif  // __Fuchsia__
#include <zircon/assert.h>
#include <zircon/errors.h>

namespace f2fs {

F2fs::F2fs(FuchsiaDispatcher dispatcher, std::unique_ptr<f2fs::Bcache> bc,
           std::unique_ptr<Superblock> sb, const MountOptions& mount_options, PlatformVfs* vfs)
    : dispatcher_(dispatcher),
      vfs_(vfs),
      bc_(std::move(bc)),
      mount_options_(mount_options),
      raw_sb_(std::move(sb)) {
#ifdef __Fuchsia__
  inspect_tree_ = std::make_unique<InspectTree>(this);
  zx::event::create(0, &fs_id_);
#endif  // __Fuchsia__
}

zx::result<std::unique_ptr<F2fs>> F2fs::Create(FuchsiaDispatcher dispatcher,
                                               std::unique_ptr<f2fs::Bcache> bc,
                                               const MountOptions& options, PlatformVfs* vfs) {
  zx::result<std::unique_ptr<Superblock>> superblock_or;
  if (superblock_or = LoadSuperblock(*bc); superblock_or.is_error()) {
    return superblock_or.take_error();
  }

  auto fs =
      std::make_unique<F2fs>(dispatcher, std::move(bc), std::move(*superblock_or), options, vfs);

  if (zx_status_t status = fs->FillSuper(); status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to initialize fs." << status;
    return zx::error(status);
  }

  return zx::ok(std::move(fs));
}

zx::result<std::unique_ptr<Superblock>> F2fs::LoadSuperblock(f2fs::Bcache& bc) {
  FsBlock block;
#ifdef __Fuchsia__
  auto buffer = block.GetData().data();
#else   // __Fuchsia__
  auto buffer = block.GetData();
#endif  // __Fuchsia__
  constexpr int kSuperblockCount = 2;
  zx_status_t status;
  for (auto i = 0; i < kSuperblockCount; ++i) {
    if (status = bc.Readblk(kSuperblockStart + i, buffer); status == ZX_OK) {
      auto superblock = std::make_unique<Superblock>();
      std::memcpy(superblock.get(), buffer + kSuperOffset, sizeof(Superblock));
      return zx::ok(std::move(superblock));
    }
  }
  FX_LOGS(ERROR) << "[f2fs] failed to read superblock." << status;
  return zx::error(status);
}

#ifdef __Fuchsia__

void F2fs::Sync(SyncCallback closure) {
  SyncFs(true);
  if (closure) {
    closure(ZX_OK);
  }
}

zx::result<fs::FilesystemInfo> F2fs::GetFilesystemInfo() {
  fs::FilesystemInfo info;

  info.block_size = kBlockSize;
  info.max_filename_size = kMaxNameLen;
  info.fs_type = fuchsia_fs::VfsType::kF2Fs;
  info.total_bytes =
      safemath::CheckMul<uint64_t>(superblock_info_->GetUserBlockCount(), kBlockSize).ValueOrDie();
  info.used_bytes = safemath::CheckMul<uint64_t>(ValidUserBlocks(), kBlockSize).ValueOrDie();
  info.total_nodes = superblock_info_->GetTotalNodeCount();
  info.used_nodes = superblock_info_->GetTotalValidInodeCount();
  info.SetFsId(fs_id_);
  info.name = "f2fs";

  // TODO(unknown): Fill free_shared_pool_bytes using fvm info

  return zx::ok(info);
}

#endif  // __Fuchsia__

void F2fs::DecValidBlockCount(VnodeF2fs* vnode, block_t count) {
  std::lock_guard lock(superblock_info_->GetStatLock());
  ZX_ASSERT(superblock_info_->GetTotalValidBlockCount() >= count);
  vnode->DecBlocks(count);
  superblock_info_->SetTotalValidBlockCount(superblock_info_->GetTotalValidBlockCount() - count);
}

zx_status_t F2fs::IncValidBlockCount(VnodeF2fs* vnode, block_t count) {
  block_t valid_block_count;
  std::lock_guard lock(superblock_info_->GetStatLock());
  valid_block_count = superblock_info_->GetTotalValidBlockCount() + count;
  if (valid_block_count > superblock_info_->GetUserBlockCount()) {
#ifdef __Fuchsia__
    inspect_tree_->OnOutOfSpace();
#endif  // __Fuchsia__
    return ZX_ERR_NO_SPACE;
  }
  vnode->IncBlocks(count);
  superblock_info_->SetTotalValidBlockCount(valid_block_count);
  superblock_info_->SetAllocValidBlockCount(superblock_info_->GetAllocValidBlockCount() + count);
  return ZX_OK;
}

block_t F2fs::ValidUserBlocks() {
  std::lock_guard lock(superblock_info_->GetStatLock());
  return superblock_info_->GetTotalValidBlockCount();
}

uint32_t F2fs::ValidNodeCount() {
  std::lock_guard lock(superblock_info_->GetStatLock());
  return superblock_info_->GetTotalValidNodeCount();
}

void F2fs::IncValidInodeCount() {
  std::lock_guard lock(superblock_info_->GetStatLock());
  ZX_ASSERT(superblock_info_->GetTotalValidInodeCount() != superblock_info_->GetTotalNodeCount());
  superblock_info_->SetTotalValidInodeCount(superblock_info_->GetTotalValidInodeCount() + 1);
}

void F2fs::DecValidInodeCount() {
  std::lock_guard lock(superblock_info_->GetStatLock());
  ZX_ASSERT(superblock_info_->GetTotalValidInodeCount());
  superblock_info_->SetTotalValidInodeCount(superblock_info_->GetTotalValidInodeCount() - 1);
}

uint32_t F2fs::ValidInodeCount() {
  std::lock_guard lock(superblock_info_->GetStatLock());
  return superblock_info_->GetTotalValidInodeCount();
}

bool F2fs::IsValid() const {
  if (bc_ == nullptr) {
    return false;
  }
  if (root_vnode_ == nullptr) {
    return false;
  }
  if (superblock_info_ == nullptr) {
    return false;
  }
  if (segment_manager_ == nullptr) {
    return false;
  }
  if (node_manager_ == nullptr) {
    return false;
  }
  if (gc_manager_ == nullptr) {
    return false;
  }
  return true;
}

// Fill the locked page with data located in the block address.
zx::result<LockedPage> F2fs::MakeReadOperation(LockedPage page, block_t blk_addr, PageType type,
                                               bool is_sync) {
  if (page->IsUptodate()) {
    return zx::ok(std::move(page));
  }
  std::vector<block_t> addrs = {blk_addr};
  std::vector<LockedPage> pages;
  pages.push_back(std::move(page));
  auto pages_or = MakeReadOperations(std::move(pages), std::move(addrs), type, is_sync);
  if (pages_or.is_error()) {
    return pages_or.take_error();
  }
  return zx::ok(std::move((*pages_or)[0]));
}

zx::result<std::vector<LockedPage>> F2fs::MakeReadOperations(std::vector<LockedPage> pages,
                                                             std::vector<block_t> addrs,
                                                             PageType type, bool is_sync) {
  return reader_->SubmitPages(std::move(pages), std::move(addrs));
}

zx_status_t F2fs::MakeTrimOperation(block_t blk_addr, block_t nblocks) const {
  return GetBc().Trim(blk_addr, nblocks);
}

}  // namespace f2fs
