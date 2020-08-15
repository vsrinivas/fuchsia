// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "factoryfs/directory.h"

#include <fuchsia/device/c/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <sys/stat.h>
#include <zircon/assert.h>

#include "factoryfs/file.h"
#include "factoryfs/superblock.h"

namespace factoryfs {

Directory::Directory(Factoryfs* fs) : factoryfs_(fs) {}

Directory::~Directory() { Device().BlockDetachVmo(std::move(vmoid_)); }

zx_status_t Directory::InitDirectoryVmo() {
  if (vmo_.is_valid()) {
    return ZX_OK;
  }

  zx_status_t status;
  const size_t vmo_size = fbl::round_up(GetSize(), kFactoryfsBlockSize);
  if ((status = zx::vmo::create(vmo_size, 0, &vmo_)) != ZX_OK) {
    FS_TRACE_ERROR("Failed to initialize vmo; error: %d\n", status);
    return status;
  }
  vmo_size_ = vmo_size;

  zx_object_set_property(vmo_.get(), ZX_PROP_NAME, "factoryfs-root", strlen("factoryfs-root"));
  if ((status = Device().BlockAttachVmo(vmo_, &vmoid_)) != ZX_OK) {
    vmo_.reset();
    return status;
  }

  Superblock info = Info();
  uint32_t dev_block_size = GetDeviceBlockInfo().block_size;
  uint32_t dev_blocks = info.directory_ent_blocks * (kFactoryfsBlockSize / dev_block_size);

  block_fifo_request_t request = {
      .opcode = BLOCKIO_READ,
      .vmoid = vmoid_.get(),
      .length = dev_blocks,
      .vmo_offset = 0,
      .dev_offset = info.directory_ent_start_block * (kFactoryfsBlockSize / dev_block_size),
  };

  return Device().FifoTransaction(&request, 1);
}

// Internal read. Usable on directories.
zx_status_t Directory::ReadInternal(void* data, size_t len, size_t off, size_t* out_actual) {
  // clip to EOF
  if (off >= GetSize()) {
    *out_actual = 0;
    return ZX_OK;
  }
  if (len > (GetSize() - off)) {
    len = GetSize() - off;
  }

  zx_status_t status = ZX_OK;
  if ((status = InitDirectoryVmo()) != ZX_OK) {
    FS_TRACE_ERROR("factoryfs: Failed to initialize VMO error:%s", zx_status_get_string(status));
    return status;
  }

  if ((status = vmo_.read(data, 0, len)) != ZX_OK) {
    FS_TRACE_ERROR("factoryfs: Failed to read VMO error:%s", zx_status_get_string(status));
    return status;
  }

  *out_actual = len;
  return ZX_OK;
}

zx_status_t Directory::IsValidDirectoryEntry(const DirectoryEntry& entry) {
  if (entry.name_len == 0 || entry.name_len > kFactoryfsMaxNameSize) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  // TODO(manalib) add more directory entry checks.
  return ZX_OK;
}

size_t Directory::GetSize() const {
  return factoryfs_->Info().directory_ent_blocks * kFactoryfsBlockSize;
}

void DumpDirectoryEntry(const DirectoryEntry* entry) {
  std::string_view entry_name(entry->name, entry->name_len);
  FS_TRACE_DEBUG("Directory entry data_len: %u\n", entry->data_len);
  FS_TRACE_DEBUG("Directory entry data_off: 0x%x\n", entry->data_off);
  FS_TRACE_DEBUG("Directory entry name: %s\n", entry_name.data());
  FS_TRACE_DEBUG("Directory entry name_len: %u\n", entry->name_len);
}

// Parses all entries in the container directory from offset 0.
// |parse_data| is guarenteed to be 4 byte aligned.
zx_status_t Directory::ParseEntries(Callback callback, void* parse_data) {
  size_t avail = GetSize();
  zx_status_t status;

  // To enforce 4 byte alignment for all the directory entries,
  // we need to enforce the starting address of parse_data is
  // also 4 byte aligned.
  uintptr_t buffer = reinterpret_cast<uintptr_t>(parse_data);
  if (buffer & 3) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  // Note about alignment: ptr is enforced to be always 4-byte aligned,
  // and DirectoryEntry itself is always 4 byte aligned i.e DirentSize will be a multiple of 4.
  // Hence it is safe to use reinterpret cast.
  while (avail > sizeof(DirectoryEntry)) {
    DirectoryEntry* entry = reinterpret_cast<DirectoryEntry*>(buffer);
    DumpDirectoryEntry(entry);
    size_t size = DirentSize(entry->name_len);
    if (size > avail) {
      FS_TRACE_ERROR("factoryfs: invalid directory entry!\n");
      return ZX_ERR_IO;
    }
    if ((status = IsValidDirectoryEntry(*entry)) != ZX_OK) {
      FS_TRACE_ERROR("factoryfs: invalid directory entry!\n");
      return status;
    }
    if ((status = callback(entry)) == ZX_OK) {
      return status;
    }
    buffer += size;
    avail -= size;
  }
  return ZX_ERR_NOT_FOUND;
}

zx_status_t Directory::LookupInternal(std::string_view filename,
                                      std::unique_ptr<DirectoryEntryManager>* out_entry) {
  if (filename.empty() || out_entry == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t dirent_blocks = Info().directory_ent_blocks;

  // We need to make sure "block" is 4 byte aligned to access directory entries.
  // and initialized to zero.
  std::vector<uint32_t> block(dirent_blocks * kFactoryfsBlockSize / 4, 0);

  uint32_t len = dirent_blocks * kFactoryfsBlockSize;

  zx_status_t status = ZX_OK;
  if ((status = InitDirectoryVmo()) != ZX_OK) {
    FS_TRACE_ERROR("factoryfs: Failed to initialize VMO error:%s", zx_status_get_string(status));
    return status;
  }

  if ((status = vmo_.read(block.data(), 0, len)) != ZX_OK) {
    FS_TRACE_ERROR("factoryfs: Failed to read VMO error:%s", zx_status_get_string(status));
    return status;
  }

  status = ParseEntries(
      [&](const DirectoryEntry* entry) {
        std::string_view entry_name(entry->name, entry->name_len);
        if (filename == entry_name) {
          return DirectoryEntryManager::Create(entry, out_entry);
        }
        return ZX_ERR_NOT_FOUND;
      },
      block.data());

  if (status != ZX_OK) {
    FS_TRACE_ERROR("factoryfs:  Directory::LookupInternal failed with error:%s",
                   zx_status_get_string(status));
  }

  return status;
}

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

zx_status_t Directory::Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name_view) {
  std::string name(name_view);
  ZX_ASSERT(name.find('/') == std::string::npos);

  if (name == ".") {
    *out = fbl::RefPtr<Directory>(this);
    return ZX_OK;
  }
  auto iter = open_vnodes_cache_.find(name);
  if (iter != open_vnodes_cache_.end()) {
    *out = fbl::RefPtr(iter->second);
    return ZX_OK;
  }

  std::unique_ptr<DirectoryEntryManager> dir_entry;
  zx_status_t status = LookupInternal(name_view, &dir_entry);
  if (status != ZX_OK) {
    return status;
  }

  auto file = fbl::AdoptRef(new File(fbl::RefPtr(this), std::move(dir_entry)));
  *out = std::move(file);
  return ZX_OK;
}

zx_status_t Directory::Close() { return ZX_OK; }

void Directory::OpenFile(std::string filename, fs::Vnode* file) {
  // TODO(manalib) check for dups
  open_vnodes_cache_.insert(make_pair(filename, file));
}

void Directory::CloseFile(std::string filename) {
  // TODO(manalib) check for non existence
  open_vnodes_cache_.erase(filename);
}

#ifdef __Fuchsia__

constexpr const char kFsName[] = "factoryfs";

zx_status_t Directory::QueryFilesystem(::llcpp::fuchsia::io::FilesystemInfo* info) {
  static_assert(fbl::constexpr_strlen(kFsName) + 1 < ::llcpp::fuchsia::io::MAX_FS_NAME_BUFFER,
                "Factoryfs name too long");
  *info = {};
  info->block_size = kFactoryfsBlockSize;
  info->max_filename_size = kFactoryfsMaxNameSize;
  info->fs_type = VFS_TYPE_FACTORYFS;
  info->fs_id = factoryfs_->GetFsIdLegacy();
  info->total_bytes = factoryfs_->Info().data_blocks * kFactoryfsBlockSize;
  info->used_bytes = factoryfs_->Info().data_blocks * kFactoryfsBlockSize;
  info->total_nodes = factoryfs_->Info().directory_entries;
  info->used_nodes = factoryfs_->Info().directory_entries;
  strlcpy(reinterpret_cast<char*>(info->name.data()), kFsName,
          ::llcpp::fuchsia::io::MAX_FS_NAME_BUFFER);
  return ZX_OK;
}

zx_status_t Directory::GetDevicePath(size_t buffer_len, char* out_name, size_t* out_len) {
  return factoryfs_->Device().GetDevicePath(buffer_len, out_name, out_len);
}
#endif
zx_status_t Directory::Unlink(fbl::StringPiece path, bool is_dir) { return ZX_ERR_NOT_SUPPORTED; }

const factoryfs::Superblock& Directory::Info() const { return factoryfs_->Info(); }

block_client::BlockDevice& Directory::Device() const { return factoryfs_->Device(); }

const fuchsia_hardware_block_BlockInfo& Directory::GetDeviceBlockInfo() const {
  return factoryfs_->GetDeviceBlockInfo();
}

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
