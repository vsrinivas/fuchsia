// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains a directory which contains blobs.

#ifndef SRC_STORAGE_BLOBFS_DIRECTORY_H_
#define SRC_STORAGE_BLOBFS_DIRECTORY_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <fuchsia/blobfs/llcpp/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>

#include <digest/digest.h>
#include <fbl/algorithm.h>
#include <fbl/ref_ptr.h>
#include <fs/vfs.h>
#include <fs/vfs_types.h>
#include <fs/vnode.h>

#include "blob-cache.h"

namespace blobfs {

class Blobfs;

// The root directory of blobfs. This directory is a flat container of all blobs in the filesystem.
#ifdef __Fuchsia__
class Directory final : public fs::Vnode, llcpp::fuchsia::blobfs::Blobfs::Interface {
#else
class Directory final : public fs::Vnode {
#endif
 public:
  explicit Directory(Blobfs* bs);
  ~Directory() final;

  ////////////////
  // fs::Vnode interface.

  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights rights,
                                     fs::VnodeRepresentation* info) final;
  fs::VnodeProtocolSet GetProtocols() const final;
  zx_status_t Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len,
                      size_t* out_actual) final;
  zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual) final;
  zx_status_t Write(const void* data, size_t len, size_t offset, size_t* out_actual) final;
  zx_status_t Append(const void* data, size_t len, size_t* out_end, size_t* out_actual) final;
  zx_status_t Lookup(fbl::StringPiece name, fbl::RefPtr<fs::Vnode>* out) final;
  zx_status_t GetAttributes(fs::VnodeAttributes* a) final;
  zx_status_t Create(fbl::StringPiece name, uint32_t mode, fbl::RefPtr<fs::Vnode>* out) final;
  zx_status_t QueryFilesystem(::llcpp::fuchsia::io::FilesystemInfo* out) final;
  zx_status_t GetDevicePath(size_t buffer_len, char* out_name, size_t* out_len) final;
  zx_status_t Unlink(fbl::StringPiece name, bool must_be_dir) final;
  void Sync(SyncCallback closure) final;

#ifdef __Fuchsia__
  void HandleFsSpecificMessage(fidl_msg_t* msg, fidl::Transaction* txn) final;
  void GetAllocatedRegions(GetAllocatedRegionsCompleter::Sync completer) final;
  void SetCorruptBlobHandler(zx::channel handler,
                             SetCorruptBlobHandlerCompleter::Sync completer) final;
#endif

 private:
  ////////////////
  // Other methods.

  BlobCache& Cache();

  Blobfs* const blobfs_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_DIRECTORY_H_
