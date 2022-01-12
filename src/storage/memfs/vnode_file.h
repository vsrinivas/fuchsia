// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MEMFS_VNODE_FILE_H_
#define SRC_STORAGE_MEMFS_VNODE_FILE_H_

#include "src/storage/memfs/vnode.h"

namespace memfs {

class VnodeFile final : public Vnode {
 public:
  explicit VnodeFile(PlatformVfs* vfs);
  ~VnodeFile() override;

  fs::VnodeProtocolSet GetProtocols() const final;

 private:
  zx_status_t CreateStream(uint32_t stream_options, zx::stream* out_stream) final;
  void DidModifyStream() final;

  zx_status_t Truncate(size_t len) final;
  zx_status_t GetAttributes(fs::VnodeAttributes* a) final;
  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights rights,
                                     fs::VnodeRepresentation* info) final;
  zx_status_t GetVmo(fuchsia_io::wire::VmoFlags flags, zx::vmo* out_vmo, size_t* out_size) final;

  zx_status_t CreateBackingStoreIfNeeded();
  size_t GetContentSize() const;

  // Ensure the underlying vmo is filled with zero from:
  // [start, round_up(end, PAGE_SIZE)).
  void ZeroTail(size_t start, size_t end);

  zx::vmo vmo_;
};

}  // namespace memfs

#endif  // SRC_STORAGE_MEMFS_VNODE_FILE_H_
