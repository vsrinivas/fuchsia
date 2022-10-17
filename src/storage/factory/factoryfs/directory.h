// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FACTORY_FACTORYFS_DIRECTORY_H_
#define SRC_STORAGE_FACTORY_FACTORYFS_DIRECTORY_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fit/function.h>
#include <lib/sync/completion.h>

#include <map>
#include <string>
#include <string_view>

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

  zx_status_t Create(std::string_view name, uint32_t mode, fbl::RefPtr<fs::Vnode>* out) final;
  zx_status_t Lookup(std::string_view name, fbl::RefPtr<fs::Vnode>* out) final;
  zx_status_t Unlink(std::string_view name, bool is_dir) final;
  zx_status_t GetAttributes(fs::VnodeAttributes* attributes) final;
  zx_status_t Rename(fbl::RefPtr<fs::Vnode> newdir, std::string_view oldname,
                     std::string_view newname, bool src_must_be_dir, bool dst_must_be_dir) final;

  zx_status_t Readdir(fs::VdirCookie* cookie, void* dirents, size_t len, size_t* out_actual) final;
  zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual) final;
  zx_status_t Write(const void* data, size_t len, size_t offset, size_t* out_actual) final;
  zx_status_t Truncate(size_t len) final;
  zx_status_t Append(const void* data, size_t len, size_t* out_end, size_t* out_actual) final;
  zx_status_t GetVmo(fuchsia_io::wire::VmoFlags flags, zx::vmo* out_vmo) final;
  zx::result<std::string> GetDevicePath() const final;
  void Sync(SyncCallback closure) final;

  // Other functions
  const factoryfs::Superblock& Info() const;

 private:
  Factoryfs& factoryfs_;
  const std::string path_;
};

}  // namespace factoryfs

#endif  // SRC_STORAGE_FACTORY_FACTORYFS_DIRECTORY_H_
