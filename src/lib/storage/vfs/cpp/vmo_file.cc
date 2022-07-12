// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/vmo_file.h"

#include <fidl/fuchsia.io/cpp/wire.h>
#include <limits.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>

#include "src/lib/storage/vfs/cpp/vfs.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"

namespace fio = fuchsia_io;

namespace fs {

VmoFile::VmoFile(zx::vmo vmo, size_t length, bool writable, VmoSharing vmo_sharing)
    : vmo_(std::move(vmo)), length_(length), writable_(writable), vmo_sharing_(vmo_sharing) {
  ZX_ASSERT(vmo_.is_valid());
}

VmoFile::~VmoFile() = default;

VnodeProtocolSet VmoFile::GetProtocols() const { return VnodeProtocol::kFile; }

bool VmoFile::ValidateRights(Rights rights) const {
  // Executable rights/VMOs are currently not supported, but may be added in the future.
  // If this is the case, we should further restrict the allowable set of rights such that
  // an executable VmoFile can only be opened as readable/executable and not writable.
  if (rights.execute) {
    return false;
  }
  return !rights.write || writable_;
}

zx_status_t VmoFile::GetAttributes(VnodeAttributes* attr) {
  *attr = VnodeAttributes();
  attr->mode = V_TYPE_FILE | V_IRUSR;
  if (writable_) {
    attr->mode |= V_IWUSR;
  }
  attr->inode = fio::wire::kInoUnknown;
  attr->content_size = length_;
  attr->storage_size = fbl::round_up(attr->content_size, zx_system_get_page_size());
  attr->link_count = 1;
  return ZX_OK;
}

zx_status_t VmoFile::Read(void* data, size_t length, size_t offset, size_t* out_actual) {
  if (length == 0u || offset >= length_) {
    *out_actual = 0u;
    return ZX_OK;
  }

  size_t remaining_length = length_ - offset;
  if (length > remaining_length) {
    length = remaining_length;
  }
  zx_status_t status = vmo_.read(data, offset, length);
  if (status != ZX_OK) {
    return status;
  }
  *out_actual = length;
  return ZX_OK;
}

zx_status_t VmoFile::Write(const void* data, size_t length, size_t offset, size_t* out_actual) {
  if (length == 0u) {
    *out_actual = 0u;
    return ZX_OK;
  }
  if (offset >= length_) {
    return ZX_ERR_NO_SPACE;
  }

  size_t remaining_length = length_ - offset;
  if (length > remaining_length) {
    length = remaining_length;
  }
  zx_status_t status = vmo_.write(data, offset, length);
  if (status == ZX_OK) {
    *out_actual = length;
  }
  return status;
}

zx_status_t VmoFile::GetNodeInfoForProtocol([[maybe_unused]] VnodeProtocol protocol, Rights rights,
                                            VnodeRepresentation* info) {
  *info = fs::VnodeRepresentation::File{};
  return ZX_OK;
}

zx_status_t VmoFile::GetVmo(fio::wire::VmoFlags flags, zx::vmo* out_vmo) {
  zx_rights_t rights = ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHT_GET_PROPERTY;
  if (flags & fio::wire::VmoFlags::kRead) {
    rights |= ZX_RIGHT_READ;
  }
  if (flags & fio::wire::VmoFlags::kWrite) {
    rights |= ZX_RIGHT_WRITE | ZX_RIGHT_SET_PROPERTY;
  }
  if (flags & fio::wire::VmoFlags::kPrivateClone) {
    zx::vmo vmo;
    if (zx_status_t status =
            vmo_.create_child(ZX_VMO_CHILD_SNAPSHOT_AT_LEAST_ON_WRITE, 0, length_, &vmo);
        status != ZX_OK) {
      return status;
    }
    return vmo.replace(rights, out_vmo);
  }
  if (flags & fio::wire::VmoFlags::kSharedBuffer) {
    return vmo_.duplicate(rights, out_vmo);
  }
  switch (vmo_sharing_) {
    case VmoSharing::NONE:
      return ZX_ERR_NOT_SUPPORTED;
    case VmoSharing::DUPLICATE:
      // As size changes are currently untracked, we remove WRITE and SET_PROPERTY rights before
      // duplicating the VMO handle. If this restriction needs to be eased in the future, size
      // changes need to be tracked accordingly, or a fixed-size child slice should be provided.
      rights &= ~(ZX_RIGHT_WRITE | ZX_RIGHT_SET_PROPERTY);
      return vmo_.duplicate(rights, out_vmo);
    case VmoSharing::CLONE_COW: {
      zx::vmo vmo;
      if (zx_status_t status =
              vmo_.create_child(ZX_VMO_CHILD_SNAPSHOT_AT_LEAST_ON_WRITE, 0, length_, &vmo);
          status != ZX_OK) {
        return status;
      }
      return vmo.replace(rights, out_vmo);
    }
  }
}

}  // namespace fs
