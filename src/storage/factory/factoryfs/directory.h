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
  explicit Directory(factoryfs::Factoryfs& fs, std::string_view path);

  Directory(const Directory&) = delete;
  Directory(Directory&&) = delete;
  Directory& operator=(const Directory&) = delete;
  Directory& operator=(Directory&&) = delete;

  ~Directory();

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

 private:
  Factoryfs& factoryfs_;
  const std::string path_;
};

}  // namespace factoryfs

#endif  // SRC_STORAGE_FACTORY_FACTORYFS_DIRECTORY_H_
