// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FACTORY_FACTORYFS_DIRECTORY_H_
#define SRC_STORAGE_FACTORY_FACTORYFS_DIRECTORY_H_

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/sync/completion.h>

#include <map>
#include <string>
#include <string_view>

#include <fbl/function.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/intrusive_single_list.h>

#include "src/storage/factory/factoryfs/directory_entry.h"
#include "src/storage/factory/factoryfs/factoryfs.h"
#include "src/storage/factory/factoryfs/format.h"

namespace factoryfs {

class Factoryfs;

class Directory final : public fs::Vnode {
 public:
  Directory(const Directory&) = delete;
  Directory(Directory&&) = delete;
  Directory& operator=(const Directory&) = delete;
  Directory& operator=(Directory&&) = delete;

  explicit Directory(factoryfs::Factoryfs* fs);
  ~Directory() final;

  // Vnode routines.
  fs::VnodeProtocolSet GetProtocols() const override { return fs::VnodeProtocol::kDirectory; }
  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights rights,
                                     fs::VnodeRepresentation* info) override {
    *info = fs::VnodeRepresentation::Directory();
    return ZX_OK;
  }

  void OpenFile(std::string name, fs::Vnode* file);
  void CloseFile(std::string name);

  zx_status_t Create(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name, uint32_t mode) final;
  zx_status_t Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) final;
  zx_status_t Unlink(fbl::StringPiece name, bool is_dir) final;
  zx_status_t GetAttributes(fs::VnodeAttributes* attributes) final;
  zx_status_t Rename(fbl::RefPtr<fs::Vnode> newdir, fbl::StringPiece oldname,
                     fbl::StringPiece newname, bool src_must_be_dir, bool dst_must_be_dir) final;

  zx_status_t Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len,
                      size_t* out_actual) final;
  zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual) final;
  zx_status_t Write(const void* data, size_t len, size_t offset, size_t* out_actual) final;
  zx_status_t Append(const void* data, size_t len, size_t* out_end, size_t* out_actual) final;
  zx_status_t QueryFilesystem(::llcpp::fuchsia::io::FilesystemInfo* out) final;
  zx_status_t GetDevicePath(size_t buffer_len, char* out_name, size_t* out_len) final;
  void Sync(SyncCallback closure) final;
  zx_status_t Close() final;

  // Other functions
  //
  const factoryfs::Superblock& Info() const;

  block_client::BlockDevice& Device() const;

  const fuchsia_hardware_block_BlockInfo& GetDeviceBlockInfo() const;

  size_t GetSize() const;
  zx_status_t ReadInternal(void* data, size_t len, size_t off, size_t* actual);

  // Vmo related operations
  zx_status_t InitDirectoryVmo(void);

  // Parses the directory and calls |callback| for each directory |entry|.
  // The callback compares filename of file to be searched and returns ZX_OK if found.
  // If a callback returns ZX_OK, the iteration stops.
  using Callback = fbl::Function<zx_status_t(const DirectoryEntry* entry)>;

  // Parses all entries in the container directory from offset 0.
  // |parse_data| is guarenteed to be 4 byte aligned.
  zx_status_t ParseEntries(Callback callback, void* parse_data);

 private:
  zx_status_t LookupInternal(std::string_view filename,
                             std::unique_ptr<DirectoryEntryManager>* out_entry);
  static zx_status_t IsValidDirectoryEntry(const DirectoryEntry& entry);

  Factoryfs* const factoryfs_;
  std::map<std::string, Vnode*> open_vnodes_cache_;
  zx::vmo vmo_{};
  uint64_t vmo_size_ = 0;
  storage::Vmoid vmoid_;
};

}  // namespace factoryfs

#endif  // SRC_STORAGE_FACTORY_FACTORYFS_DIRECTORY_H_
