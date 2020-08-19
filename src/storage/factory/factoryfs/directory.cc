// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/factory/factoryfs/directory.h"

#include <fuchsia/device/c/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <sys/stat.h>
#include <zircon/assert.h>

#include <string_view>

#include "src/storage/factory/factoryfs/superblock.h"

namespace factoryfs {

Directory::Directory(factoryfs::Factoryfs& fs, std::string_view path)
    : factoryfs_(fs), path_(path) {
  factoryfs_.DidOpen(path_, *this);
}

Directory::~Directory() { factoryfs_.DidClose(path_); }

zx_status_t Directory::Create(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name_view,
                              uint32_t mode) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Directory::Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len,
                               size_t* out_actual) {
  // TODO(manalib)
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Directory::Read(void* data, size_t len, size_t off, size_t* out_actual) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Directory::Write(const void* data, size_t len, size_t offset, size_t* out_actual) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Directory::Append(const void* data, size_t len, size_t* out_end, size_t* out_actual) {
  return ZX_ERR_NOT_SUPPORTED;
}

void Directory::Sync(SyncCallback closure) {}

zx_status_t Directory::Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) {
  ZX_ASSERT(name.find('/') == std::string::npos);

  if (name == ".") {
    *out = fbl::RefPtr<Directory>(this);
    return ZX_OK;
  }

  std::string full_path;
  if (!path_.empty()) {
    full_path.reserve(path_.size() + 1 + name.size());
    full_path = path_;
    full_path.push_back('/');
    full_path.append(name);
    name = full_path;
  }

  auto result = factoryfs_.Lookup(name);
  if (result.is_error()) {
    return result.status_value();
  }
  *out = std::move(result).value();
  return ZX_OK;
}

zx_status_t Directory::Close() { return ZX_OK; }

#ifdef __Fuchsia__

constexpr const char kFsName[] = "factoryfs";

zx_status_t Directory::QueryFilesystem(::llcpp::fuchsia::io::FilesystemInfo* info) {
  static_assert(fbl::constexpr_strlen(kFsName) + 1 < ::llcpp::fuchsia::io::MAX_FS_NAME_BUFFER,
                "Factoryfs name too long");
  *info = {};
  info->block_size = kFactoryfsBlockSize;
  info->max_filename_size = kFactoryfsMaxNameSize;
  info->fs_type = VFS_TYPE_FACTORYFS;
  info->fs_id = factoryfs_.GetFsIdLegacy();
  info->total_bytes = Info().data_blocks * kFactoryfsBlockSize;
  info->used_bytes = Info().data_blocks * kFactoryfsBlockSize;
  info->total_nodes = Info().directory_entries;
  info->used_nodes = Info().directory_entries;
  strlcpy(reinterpret_cast<char*>(info->name.data()), kFsName,
          ::llcpp::fuchsia::io::MAX_FS_NAME_BUFFER);
  return ZX_OK;
}

zx_status_t Directory::GetDevicePath(size_t buffer_len, char* out_name, size_t* out_len) {
  return factoryfs_.Device().GetDevicePath(buffer_len, out_name, out_len);
}
#endif
zx_status_t Directory::Unlink(fbl::StringPiece path, bool is_dir) { return ZX_ERR_NOT_SUPPORTED; }

const factoryfs::Superblock& Directory::Info() const { return factoryfs_.Info(); }

zx_status_t Directory::GetAttributes(fs::VnodeAttributes* attributes) {
  *attributes = fs::VnodeAttributes();
  attributes->mode = (V_TYPE_DIR | V_IRUSR);
  attributes->inode = ::llcpp::fuchsia::io::INO_UNKNOWN;
  attributes->content_size = Info().directory_ent_blocks * kFactoryfsBlockSize;
  attributes->storage_size = Info().directory_ent_blocks * kFactoryfsBlockSize;
  attributes->link_count = 1;
  attributes->creation_time = 0;
  attributes->modification_time = 0;
  return ZX_OK;
}

zx_status_t Directory::Rename(fbl::RefPtr<fs::Vnode> newdirectory, fbl::StringPiece currname,
                              fbl::StringPiece newname, bool srcdir, bool dstdir) {
  return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace factoryfs
