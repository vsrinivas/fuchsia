// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains a directory which contains blobs.

#ifndef SRC_STORAGE_BLOBFS_DIRECTORY_H_
#define SRC_STORAGE_BLOBFS_DIRECTORY_H_

#include <fidl/fuchsia.io/cpp/wire.h>

#include <string_view>

#include <fbl/algorithm.h>
#include <fbl/ref_ptr.h>

#include "src/lib/storage/vfs/cpp/vfs.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

namespace blobfs {

class Blobfs;

// The root directory of blobfs. This directory is a flat container of all blobs in the filesystem.
class Directory final : public fs::Vnode {
 public:
  explicit Directory(Blobfs* bs);
  ~Directory() final;

  // fs::Vnode interface.
  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights rights,
                                     fs::VnodeRepresentation* info) final;
  fs::VnodeProtocolSet GetProtocols() const final;
  zx_status_t Readdir(fs::VdirCookie* cookie, void* dirents, size_t len, size_t* out_actual) final;
  zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual) final;
  zx_status_t Write(const void* data, size_t len, size_t offset, size_t* out_actual) final;
  zx_status_t Append(const void* data, size_t len, size_t* out_end, size_t* out_actual) final;
  zx_status_t Lookup(std::string_view name, fbl::RefPtr<fs::Vnode>* out) final;
  zx_status_t GetAttributes(fs::VnodeAttributes* a) final;
  zx_status_t Create(std::string_view name, uint32_t mode, fbl::RefPtr<fs::Vnode>* out) final;
  zx::status<std::string> GetDevicePath() const final;
  zx_status_t Unlink(std::string_view name, bool must_be_dir) final;
  void Sync(SyncCallback closure) final;

 private:
  Blobfs* const blobfs_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_DIRECTORY_H_
