// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/memfs/memfs.h"

#include <safemath/safe_math.h>

#include "src/storage/memfs/dnode.h"
#include "src/storage/memfs/vnode_dir.h"

namespace memfs {

size_t GetPageSize() {
  static const size_t kPageSize = static_cast<size_t>(zx_system_get_page_size());
  return kPageSize;
}

zx_status_t Memfs::GrowVMO(zx::vmo& vmo, size_t current_size, size_t request_size,
                           size_t* actual_size) {
  if (request_size <= current_size) {
    *actual_size = current_size;
    return ZX_OK;
  }

  size_t page_size = GetPageSize();
  size_t aligned_len = fbl::round_up(request_size, page_size);
  ZX_DEBUG_ASSERT(current_size % page_size == 0);

  if (!vmo.is_valid()) {
    if (zx_status_t status = zx::vmo::create(aligned_len, ZX_VMO_RESIZABLE, &vmo);
        status != ZX_OK) {
      return status;
    }
  } else {
    if (zx_status_t status = vmo.set_size(aligned_len); status != ZX_OK) {
      return status;
    }
  }
  // vmo operation succeeded
  *actual_size = aligned_len;
  return ZX_OK;
}

zx::result<fs::FilesystemInfo> Memfs::GetFilesystemInfo() {
  fs::FilesystemInfo info;

  info.block_size = safemath::checked_cast<uint32_t>(GetPageSize());
  info.max_filename_size = kDnodeNameMax;
  info.fs_type = fuchsia_fs::VfsType::kMemfs;
  info.SetFsId(fs_id_);

  // TODO(fxbug.dev/86984) Define a better value for "unknown" or "undefined" for the total_bytes
  // and used_bytes (memfs vends writable duplicates of its underlying VMOs to its clients which
  // makes accounting difficult).
  info.total_bytes = UINT64_MAX;
  info.used_bytes = 0;
  info.total_nodes = UINT64_MAX;
  uint64_t deleted_ino_count = Vnode::GetDeletedInoCounter();
  uint64_t ino_count = Vnode::GetInoCounter();
  ZX_DEBUG_ASSERT(ino_count >= deleted_ino_count);
  info.used_nodes = ino_count - deleted_ino_count;
  info.name = "memfs";

  return zx::ok(info);
}

zx_status_t Memfs::Create(async_dispatcher_t* dispatcher, std::string_view fs_name,
                          std::unique_ptr<memfs::Memfs>* out_vfs, fbl::RefPtr<VnodeDir>* out_root) {
  return CreateWithOptions(dispatcher, fs_name, Options(), out_vfs, out_root);
}

zx_status_t Memfs::CreateWithOptions(async_dispatcher_t* dispatcher, std::string_view fs_name,
                                     Memfs::Options options, std::unique_ptr<memfs::Memfs>* out_vfs,
                                     fbl::RefPtr<VnodeDir>* out_root) {
  auto fs = std::unique_ptr<memfs::Memfs>(new memfs::Memfs(dispatcher));

  fbl::RefPtr<VnodeDir> root = fbl::MakeRefCounted<VnodeDir>(options.max_file_size);
  std::unique_ptr<Dnode> dn = Dnode::Create(fs_name, root);
  root->dnode_ = dn.get();
  root->dnode_parent_ = dn.get()->GetParent();
  fs->root_ = std::move(dn);

  if (zx_status_t status = zx::event::create(0, &fs->fs_id_); status != ZX_OK)
    return status;

  *out_root = std::move(root);
  *out_vfs = std::move(fs);
  return ZX_OK;
}

Memfs::Memfs(async_dispatcher_t* dispatcher) : fs::ManagedVfs(dispatcher) {}

Memfs::~Memfs() = default;

zx_status_t Memfs::CreateFromVmo(VnodeDir* parent, std::string_view name, zx_handle_t vmo,
                                 zx_off_t off, zx_off_t len) {
  std::lock_guard<std::mutex> lock(vfs_lock_);
  return parent->CreateFromVmo(name, vmo, off, len);
}

}  // namespace memfs
