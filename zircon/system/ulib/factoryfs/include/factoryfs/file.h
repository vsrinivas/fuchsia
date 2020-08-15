// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FACTORYFS_FILE_H_
#define FACTORYFS_FILE_H_

#include <fbl/ref_ptr.h>
#include <storage/buffer/vmoid_registry.h>

#include "factoryfs/directory_entry.h"
#include "factoryfs/factoryfs.h"

namespace factoryfs {

class File final : public fs::Vnode {
 public:
  File(const File&) = delete;
  File(File&&) = delete;
  File& operator=(const File&) = delete;
  File& operator=(File&&) = delete;

  ~File() final;
  // Vnode routines.
  fs::VnodeProtocolSet GetProtocols() const override { return fs::VnodeProtocol::kFile; }
  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights rights,
                                     fs::VnodeRepresentation* info) override {
    *info = fs::VnodeRepresentation::File();
    return ZX_OK;
  }

  zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual) override;
  zx_status_t Write(const void* data, size_t len, size_t offset, size_t* out_actual) override;
  zx_status_t Truncate(size_t len) override;
  zx_status_t GetAttributes(fs::VnodeAttributes* attributes) override;
  zx_status_t Close() override;

  // Gets file size in bytes.
  uint32_t GetSize() const;

  // Gets file name.
  std::string_view GetName() const;

  zx_status_t InitFileVmo();
  explicit File(fbl::RefPtr<Directory> root_dir, std::unique_ptr<DirectoryEntryManager> entry);

 private:
  fbl::RefPtr<Directory> root_dir_;
  zx::vmo vmo_{};
  uint64_t vmo_size_ = 0;
  storage::Vmoid vmoid_;
  std::unique_ptr<DirectoryEntryManager> directory_entry_;
};

}  // namespace factoryfs

#endif  // FACTORYFS_FILE_H_
