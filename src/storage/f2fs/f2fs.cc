// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

#include <fcntl.h>
#include <inttypes.h>
#ifdef __Fuchsia__
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/trace-provider/provider.h>
#include <lib/zx/event.h>
#endif  // __Fuchsia__
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <memory>

#ifdef __Fuchsia__
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#endif  // __Fuchsia__
#include "src/lib/storage/vfs/cpp/trace.h"

namespace f2fs {

#ifdef __Fuchsia__
F2fs::F2fs(async_dispatcher_t* dispatcher, std::unique_ptr<f2fs::Bcache> bc,
           std::unique_ptr<Superblock> sb, const MountOptions& mount_options)
    : fs::PagedVfs(dispatcher),
      bc_(std::move(bc)),
      mount_options_(mount_options),
      raw_sb_(std::move(sb)),
      inspect_tree_(this) {
  zx::event::create(0, &fs_id_);
}
#else   // __Fuchsia__
F2fs::F2fs(std::unique_ptr<f2fs::Bcache> bc, std::unique_ptr<Superblock> sb,
           const MountOptions& mount_options)
    : bc_(std::move(bc)), mount_options_(mount_options), raw_sb_(std::move(sb)) {}
#endif  // __Fuchsia__

F2fs::~F2fs() {
  // Inform PagedVfs so that it can stop threads that might call out to f2fs.
  TearDown();
}

#ifdef __Fuchsia__
zx_status_t F2fs::Create(async_dispatcher_t* dispatcher, std::unique_ptr<f2fs::Bcache> bc,
                         const MountOptions& options, std::unique_ptr<F2fs>* out) {
#else   // __Fuchsia__
zx_status_t F2fs::Create(std::unique_ptr<f2fs::Bcache> bc, const MountOptions& options,
                         std::unique_ptr<F2fs>* out) {
#endif  // __Fuchsia__
  auto info = std::make_unique<Superblock>();
  if (zx_status_t status = LoadSuperblock(bc.get(), info.get()); status != ZX_OK) {
    return status;
  }

#ifdef __Fuchsia__
  *out = std::make_unique<F2fs>(dispatcher, std::move(bc), std::move(info), options);
  // Create Pager and PagerPool
  if (auto status = (*out)->Init(); status.is_error())
    return status.status_value();
#else   // __Fuchsia__
  *out = std::unique_ptr<F2fs>(new F2fs(std::move(bc), std::move(info), options));
#endif  // __Fuchsia__

  if (zx_status_t status = (*out)->FillSuper(); status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to initialize fs." << status;
    return status;
  }

  return ZX_OK;
}

zx_status_t LoadSuperblock(f2fs::Bcache* bc, Superblock* out_info, block_t bno) {
  ZX_ASSERT(bno <= 1);
  FsBlock super_block;
#ifdef __Fuchsia__
  if (zx_status_t status = bc->Readblk(bno, super_block.GetData().data()); status != ZX_OK) {
#else   // __Fuchsia__
  if (zx_status_t status = bc->Readblk(bno, super_block.GetData()); status != ZX_OK) {
#endif  // __Fuchsia__
    return status;
  }
#ifdef __Fuchsia__
  memcpy(out_info, static_cast<uint8_t*>(super_block.GetData().data()) + kSuperOffset,
         sizeof(Superblock));
#else   // __Fuchsia__
  memcpy(out_info, static_cast<uint8_t*>(super_block.GetData()) + kSuperOffset, sizeof(Superblock));
#endif  // __Fuchsia__
  return ZX_OK;
}

zx_status_t LoadSuperblock(f2fs::Bcache* bc, Superblock* out_info) {
  if (zx_status_t status = LoadSuperblock(bc, out_info, kSuperblockStart); status != ZX_OK) {
    if (zx_status_t status = LoadSuperblock(bc, out_info, kSuperblockStart + 1); status != ZX_OK) {
      FX_LOGS(ERROR) << "failed to read superblock." << status;
      return status;
    }
  }
  return ZX_OK;
}

#ifdef __Fuchsia__
zx::status<std::unique_ptr<F2fs>> CreateFsAndRoot(const MountOptions& mount_options,
                                                  async_dispatcher_t* dispatcher,
                                                  std::unique_ptr<f2fs::Bcache> bcache,
                                                  fidl::ServerEnd<fuchsia_io::Directory> root,
                                                  fit::closure on_unmount,
                                                  ServeLayout serve_layout) {
#else   // __Fuchsia__
zx::status<std::unique_ptr<F2fs>> CreateFsAndRoot(const MountOptions& mount_options,
                                                  std::unique_ptr<f2fs::Bcache> bcache) {
#endif  // __Fuchsia__
  TRACE_DURATION("f2fs", "CreateFsAndRoot");

  std::unique_ptr<F2fs> fs;
#ifdef __Fuchsia__
  if (zx_status_t status = F2fs::Create(dispatcher, std::move(bcache), mount_options, &fs);
      status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to create filesystem object " << status;
    return zx::error(status);
  }
#else   // __Fuchsia__
  if (zx_status_t status = F2fs::Create(std::move(bcache), mount_options, &fs); status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to create filesystem object " << status;
    return zx::error(status);
  }
#endif  // __Fuchsia__

  fbl::RefPtr<VnodeF2fs> data_root;
  if (zx_status_t status = VnodeF2fs::Vget(fs.get(), fs->RawSb().root_ino, &data_root);
      status != ZX_OK) {
    FX_LOGS(ERROR) << "cannot find root inode: " << status;
    return zx::error(status);
  }

#ifdef __Fuchsia__
  fs->SetUnmountCallback(std::move(on_unmount));

  fbl::RefPtr<fs::Vnode> export_root;
  switch (serve_layout) {
    case ServeLayout::kDataRootOnly:
      export_root = std::move(data_root);
      break;
    case ServeLayout::kExportDirectory:
      auto outgoing = fbl::MakeRefCounted<fs::PseudoDir>(fs.get());
      outgoing->AddEntry("root", std::move(data_root));

      auto svc_dir = fbl::MakeRefCounted<fs::PseudoDir>(fs.get());
      outgoing->AddEntry("svc", svc_dir);

      auto query_svc = fbl::MakeRefCounted<fs::QueryService>(fs.get());
      svc_dir->AddEntry(fidl::DiscoverableProtocolName<fuchsia_fs::Query>, query_svc);
      fs->SetQueryService(std::move(query_svc));

      auto admin_svc = fbl::MakeRefCounted<AdminService>(fs->dispatcher(), fs.get());
      svc_dir->AddEntry(fidl::DiscoverableProtocolName<fuchsia_fs::Admin>, admin_svc);
      fs->SetAdminService(std::move(admin_svc));

      fs->GetInspectTree().Initialize();

      inspect::TreeHandlerSettings settings{
          .snapshot_behavior = inspect::TreeServerSendPreference::Frozen(
              inspect::TreeServerSendPreference::Type::DeepCopy)};

      auto inspect_tree = fbl::MakeRefCounted<fs::Service>(
          [connector = inspect::MakeTreeHandler(&fs->GetInspectTree().GetInspector(), dispatcher,
                                                settings)](zx::channel chan) mutable {
            connector(fidl::InterfaceRequest<fuchsia::inspect::Tree>(std::move(chan)));
            return ZX_OK;
          });

      auto diagnostics_dir = fbl::MakeRefCounted<fs::PseudoDir>(fs.get());
      outgoing->AddEntry("diagnostics", diagnostics_dir);
      diagnostics_dir->AddEntry(fuchsia::inspect::Tree::Name_, inspect_tree);

      export_root = std::move(outgoing);
      break;
  }

  if (zx_status_t status = fs->ServeDirectory(std::move(export_root), std::move(root));
      status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to establish mount_channel" << status;
    return zx::error(status);
  }
#endif  // __Fuchsia__

  return zx::ok(std::move(fs));
}

#ifdef __Fuchsia__
void F2fs::Sync(SyncCallback closure) {
  SyncFs(true);
  if (closure) {
    closure(ZX_OK);
  }
}

void F2fs::Shutdown(fs::FuchsiaVfs::ShutdownCallback cb) {
  PagedVfs::Shutdown([this, cb = std::move(cb)](zx_status_t status) mutable {
    Sync([this, status, cb = std::move(cb)](zx_status_t) mutable {
      async::PostTask(dispatcher(), [this, status, cb = std::move(cb)]() mutable {
        PutSuper();
        bc_.reset();
        // Identify to the unmounting thread that teardown is complete.
        if (on_unmount_) {
          on_unmount_();
        }
        // Identify to the unmounting channel that teardown is complete.
        cb(status);
      });
    });
  });
}

void F2fs::OnNoConnections() {
  if (IsTerminating()) {
    return;
  }
  UninstallAll(zx::time::infinite());
  Shutdown([](zx_status_t status) mutable {
    ZX_ASSERT_MSG(status == ZX_OK, "Filesystem shutdown failed on OnNoConnections(): %s",
                  zx_status_get_string(status));
  });
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
    inspect_tree_.OnOutOfSpace();
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

#ifdef __Fuchsia__

zx::status<fs::FilesystemInfo> F2fs::GetFilesystemInfo() {
  fs::FilesystemInfo info;

  info.block_size = kBlockSize;
  info.max_filename_size = kMaxNameLen;
  info.fs_type = VFS_TYPE_F2FS;
  info.total_bytes = superblock_info_->GetUserBlockCount() * kBlockSize;
  info.used_bytes = ValidUserBlocks() * kBlockSize;
  info.total_nodes = superblock_info_->GetTotalNodeCount();
  info.used_nodes = superblock_info_->GetTotalValidInodeCount();
  info.SetFsId(fs_id_);
  info.name = "f2fs";

  // TODO(unknown): Fill free_shared_pool_bytes using fvm info

  return zx::ok(info);
}

#endif  // __Fuchsia__

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
  return true;
}

// Fill the locked page with data located in the block address.
zx_status_t F2fs::MakeReadOperation(fbl::RefPtr<Page> page, block_t blk_addr, bool is_sync) {
  if (page->IsUptodate()) {
    return ZX_OK;
  }

  if (blk_addr >= GetBc().Maxblk()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  fs::BufferedOperationsBuilder operations;
  operations.Add(
      storage::Operation{
          .type = storage::OperationType::kRead,
          .vmo_offset = 0,
          .dev_offset = blk_addr,
          .length = 1,
      },
      page.get());

  // block_client::Client::Transaction() is thread safe.
  zx_status_t ret = GetBc().RunRequests(operations.TakeOperations());
  if (ret == ZX_OK && is_sync) {
    page->SetUptodate();
  }
  return ret;
}

zx_status_t F2fs::MakeWriteOperation(fbl::RefPtr<Page> page, block_t blk_addr, PageType type) {
  if (blk_addr >= GetBc().Maxblk()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  // TODO: writer_ needs to keep merged IOs separately for each type.
  writer_->EnqueuePage(
      storage::Operation{
          .type = storage::OperationType::kWrite,
          .vmo_offset = 0,
          .dev_offset = blk_addr,
          .length = 1,
      },
      std::move(page));

  return ZX_OK;
}

zx_status_t F2fs::MakeOperation(storage::OperationType op, fbl::RefPtr<Page> page, block_t blk_addr,
                                PageType type, block_t nblocks) {
  // TODO: verify_block_addr(superblock_info, blk_addr);
  switch (op) {
    case storage::OperationType::kWrite:
      return MakeWriteOperation(std::move(page), blk_addr, type);
    case storage::OperationType::kRead:
      return MakeReadOperation(std::move(page), blk_addr, true);
    case storage::OperationType::kTrim:
      return GetBc().Trim(blk_addr, nblocks);
    default:
      return ZX_ERR_INVALID_ARGS;
  };
}

}  // namespace f2fs
