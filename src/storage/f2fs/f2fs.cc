// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

#include <fcntl.h>
#include <inttypes.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/trace-provider/provider.h>
#include <lib/zx/event.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <memory>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/trace.h"

namespace f2fs {

F2fs::F2fs(std::unique_ptr<f2fs::Bcache> bc, SuperBlock* sb, const MountOptions& mount_options)
    : bc_(std::move(bc)), mount_options_(mount_options) {
  raw_sb_ = std::unique_ptr<SuperBlock>(sb);

  zx::event event;
  if (zx_status_t status = zx::event::create(0, &event); status == ZX_OK) {
    zx_info_handle_basic_t info;
    if (status = event.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
        status == ZX_OK) {
      fs_id_ = std::move(event);
      fs_id_legacy_ = info.koid;
    }
  }
}

F2fs::~F2fs() {}

zx_status_t F2fs::Create(std::unique_ptr<f2fs::Bcache> bc, const MountOptions& options,
                         std::unique_ptr<F2fs>* out) {
  SuperBlock* info;

  info = new SuperBlock();
  if (zx_status_t status = LoadSuperblock(bc.get(), info); status != ZX_OK) {
    return status;
  }

  *out = std::unique_ptr<F2fs>(new F2fs(std::move(bc), info, options));

  if (zx_status_t status = (*out)->FillSuper(); status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to initialize fs." << status;
    return status;
  }

  return ZX_OK;
}

zx_status_t LoadSuperblock(f2fs::Bcache* bc, SuperBlock* out_info, block_t bno) {
  // TODO: define ino for superblock after cache impl.
  // the 1st and the 2nd blocks each have a identical Superblock.
  ZX_ASSERT(bno <= 1);
  Page* page = GrabCachePage(nullptr, 0, 0);
  if (zx_status_t status = bc->Readblk(kSuperblockStart + bno, PageAddress(page));
      status != ZX_OK) {
    F2fsPutPage(page, 1);
    return status;
  }
  memcpy(out_info, static_cast<uint8_t*>(PageAddress(page)) + kSuperOffset, sizeof(SuperBlock));
  F2fsPutPage(page, 1);
  return ZX_OK;
}

zx_status_t LoadSuperblock(f2fs::Bcache* bc, SuperBlock* out_info) {
  // TODO: define ino for superblock after cache impl.
  if (zx_status_t status = LoadSuperblock(bc, out_info, kSuperblockStart); status != ZX_OK) {
    if (zx_status_t status = LoadSuperblock(bc, out_info, kSuperblockStart + 1); status != ZX_OK) {
      FX_LOGS(ERROR) << "failed to read superblock." << status;
      return status;
    }
  }
  return ZX_OK;
}

zx::status<std::unique_ptr<F2fs>> CreateFsAndRoot(const MountOptions& mount_options,
                                                  async_dispatcher_t* dispatcher,
                                                  std::unique_ptr<f2fs::Bcache> bcache,
                                                  fidl::ServerEnd<fuchsia_io::Directory> root,
                                                  fbl::Closure on_unmount,
                                                  ServeLayout serve_layout) {
  TRACE_DURATION("f2fs", "CreateFsAndRoot");

  std::unique_ptr<F2fs> fs;
  if (zx_status_t status = F2fs::Create(std::move(bcache), mount_options, &fs); status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to create filesystem object " << status;
    return zx::error(status);
  }

  fbl::RefPtr<VnodeF2fs> data_root;
  if (zx_status_t status = VnodeF2fs::Vget(fs.get(), fs->RawSb().root_ino, &data_root);
      status != ZX_OK) {
    FX_LOGS(ERROR) << "cannot find root inode: " << status;
    return zx::error(status);
  }

  fs->SetUnmountCallback(std::move(on_unmount));
  fs->SetDispatcher(dispatcher);

  fbl::RefPtr<fs::Vnode> export_root;
  switch (serve_layout) {
    case ServeLayout::kDataRootOnly:
      export_root = std::move(data_root);
      break;
    case ServeLayout::kExportDirectory:
      auto outgoing = fbl::MakeRefCounted<fs::PseudoDir>();
      outgoing->AddEntry("root", std::move(data_root));

      auto svc_dir = fbl::MakeRefCounted<fs::PseudoDir>();
      outgoing->AddEntry("svc", svc_dir);

      auto query_svc = fbl::MakeRefCounted<QueryService>(fs->dispatcher(), fs.get());
      svc_dir->AddEntry(fidl::DiscoverableProtocolName<fuchsia_fs::Query>, query_svc);
      fs->SetQueryService(std::move(query_svc));

      auto admin_svc = fbl::MakeRefCounted<AdminService>(fs->dispatcher(), fs.get());
      svc_dir->AddEntry(fidl::DiscoverableProtocolName<fuchsia_fs::Admin>, admin_svc);
      fs->SetAdminService(std::move(admin_svc));

      export_root = std::move(outgoing);
      break;
  }

  FX_LOGS(INFO) << "CreateFsAndRoot";

  if (zx_status_t status = fs->ServeDirectory(std::move(export_root), std::move(root));
      status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to establish mount_channel" << status;
    return zx::error(status);
  }

  return zx::ok(std::move(fs));
}

void Sync(SyncCallback closure) {
  if (closure)
    closure(ZX_OK);
}

void F2fs::Shutdown(fs::Vfs::ShutdownCallback cb) {
  ManagedVfs::Shutdown([this, cb = std::move(cb)](zx_status_t status) mutable {
    Sync([this, cb = std::move(cb)](zx_status_t) mutable {
      async::PostTask(dispatcher(), [this, cb = std::move(cb)]() mutable {
        auto on_unmount = std::move(on_unmount_);

        PutSuper();

        bc_.reset();

        // Identify to the unmounting channel that teardown is complete.
        cb(ZX_OK);

        // Identify to the unmounting thread that teardown is complete.
        if (on_unmount) {
          on_unmount();
        }
      });
    });
  });
}

void F2fs::DecValidBlockCount(VnodeF2fs* vnode, block_t count) {
  fbl::AutoLock lock(&sbi_->stat_lock);
  ZX_ASSERT(sbi_->total_valid_block_count >= count);
  vnode->DecBlocks(count);
  sbi_->total_valid_block_count -= count;
}

zx_status_t F2fs::IncValidBlockCount(VnodeF2fs* vnode, block_t count) {
  block_t valid_block_count;
  fbl::AutoLock lock(&sbi_->stat_lock);
  valid_block_count = sbi_->total_valid_block_count + count;
  if (valid_block_count > sbi_->user_block_count) {
    return ZX_ERR_NO_SPACE;
  }
  vnode->IncBlocks(count);
  sbi_->total_valid_block_count = valid_block_count;
  sbi_->alloc_valid_block_count += count;
  return ZX_OK;
}

block_t F2fs::ValidUserBlocks() {
  fbl::AutoLock lock(&sbi_->stat_lock);
  return sbi_->total_valid_block_count;
}

uint64_t F2fs::ValidNodeCount() {
  fbl::AutoLock lock(&sbi_->stat_lock);
  return sbi_->total_valid_node_count;
}

void F2fs::IncValidInodeCount() {
  fbl::AutoLock lock(&sbi_->stat_lock);
  ZX_ASSERT(sbi_->total_valid_inode_count != sbi_->total_node_count);
  sbi_->total_valid_inode_count++;
}

void F2fs::DecValidInodeCount() {
  fbl::AutoLock lock(&sbi_->stat_lock);
  ZX_ASSERT(sbi_->total_valid_inode_count);
  sbi_->total_valid_inode_count--;
}

uint64_t F2fs::ValidInodeCount() {
  fbl::AutoLock lock(&sbi_->stat_lock);
  return sbi_->total_valid_inode_count;
}

// Check whether the given nid is within node id range.
void F2fs::CheckNidRange(const nid_t& nid) { ZX_ASSERT(nid < sbi_->nm_info->max_nid); }

zx_status_t FlushDirtyNodePage(F2fs* fs, Page* page) {
  SbInfo& sbi = fs->GetSbInfo();

  if (!page)
    return ZX_OK;

  ZX_ASSERT(page->host == nullptr);
  ZX_ASSERT(page->host_nid == NodeIno(&sbi));

  if (zx_status_t ret = fs->Nodemgr().F2fsWriteNodePage(page, nullptr); ret != ZX_OK) {
    FX_LOGS(ERROR) << "Node page write error " << ret;
    return ret;
  }

  return ZX_OK;
}

zx_status_t F2fs::GetFsId(zx::event* out_fs_id) const {
  ZX_DEBUG_ASSERT(fs_id_.is_valid());
  return fs_id_.duplicate(ZX_RIGHTS_BASIC, out_fs_id);
}

zx_status_t FlushDirtyMetaPage(F2fs* fs, Page* page) {
  SbInfo& sbi = fs->GetSbInfo();

  if (!page)
    return ZX_OK;

  ZX_ASSERT(page->host == nullptr);
  ZX_ASSERT(page->host_nid == MetaIno(&sbi));

  if (zx_status_t ret = fs->F2fsWriteMetaPage(page, nullptr); ret != ZX_OK) {
    FX_LOGS(ERROR) << "Meta page write error " << ret;
    return ret;
  }

  return ZX_OK;
}

zx_status_t FlushDirtyDataPage(F2fs* fs, Page* page) {
  if (!page)
    return ZX_OK;

  ZX_ASSERT(page->host != nullptr);

  VnodeF2fs* vnode = static_cast<VnodeF2fs*>(page->host);

  if (zx_status_t ret = vnode->WriteDataPageReq(page, nullptr); ret != ZX_OK) {
    FX_LOGS(ERROR) << "Data page write error " << ret;
    return ret;
  }

  return ZX_OK;
}

}  // namespace f2fs
