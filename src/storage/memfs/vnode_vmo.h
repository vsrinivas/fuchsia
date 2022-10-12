// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MEMFS_VNODE_VMO_H_
#define SRC_STORAGE_MEMFS_VNODE_VMO_H_

#include "src/storage/memfs/vnode.h"

namespace memfs {

class VnodeVmo final : public Vnode {
 public:
  VnodeVmo(zx_handle_t vmo, zx_off_t offset, zx_off_t length);
  ~VnodeVmo() override;

  fs::VnodeProtocolSet GetProtocols() const final;
  bool ValidateRights(fs::Rights rights) const final;

 private:
  zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual) final;
  zx_status_t GetAttributes(fs::VnodeAttributes* a) final;
  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights rights,
                                     fs::VnodeRepresentation* info) final;
  zx_status_t GetVmo(fuchsia_io::wire::VmoFlags flags, zx::vmo* out_vmo) final;
  zx_status_t MakeLocalClone();

  zx_handle_t vmo_ = ZX_HANDLE_INVALID;
  zx_off_t offset_ = 0;
  zx_off_t length_ = 0;
  bool executable_ = false;
  bool have_local_clone_ = false;
};

}  // namespace memfs

#endif  // SRC_STORAGE_MEMFS_VNODE_VMO_H_
