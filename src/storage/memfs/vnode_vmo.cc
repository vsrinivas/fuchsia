// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/memfs/vnode_vmo.h"

#include <lib/syslog/cpp/macros.h>

#include "src/storage/memfs/dnode.h"
#include "src/storage/memfs/memfs.h"

namespace memfs {
namespace {

bool WindowMatchesVMO(zx_handle_t vmo, zx_off_t offset, zx_off_t length) {
  if (offset != 0)
    return false;
  uint64_t size;
  if (zx_object_get_property(vmo, ZX_PROP_VMO_CONTENT_SIZE, &size, sizeof(size)) == ZX_OK) {
    return size == length;
  }
  if (zx_vmo_get_size(vmo, &size) < 0)
    return false;
  return size == length;
}

}  // namespace

VnodeVmo::VnodeVmo(zx_handle_t vmo, zx_off_t offset, zx_off_t length)
    : vmo_(vmo), offset_(offset), length_(length) {
  // Check whether the backing VMO has ZX_RIGHT_EXECUTE, which influences later validation and
  // behavior.
  zx_info_handle_basic_t handle_info;
  zx_status_t status = zx_object_get_info(vmo_, ZX_INFO_HANDLE_BASIC, &handle_info,
                                          sizeof(handle_info), nullptr, nullptr);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "zx_object_get_info failed in VnodeVmo constructor: " << status;
    return;
  }
  executable_ = ((handle_info.rights & ZX_RIGHT_EXECUTE) != 0);
}

VnodeVmo::~VnodeVmo() {
  if (have_local_clone_) {
    zx_handle_close(vmo_);
  }
}

fs::VnodeProtocolSet VnodeVmo::GetProtocols() const { return fs::VnodeProtocol::kFile; }

bool VnodeVmo::ValidateRights(fs::Rights rights) const {
  return !rights.write && (!rights.execute || executable_);
}

zx_status_t VnodeVmo::GetNodeInfoForProtocol([[maybe_unused]] fs::VnodeProtocol protocol,
                                             fs::Rights rights, fs::VnodeRepresentation* info) {
  *info = fs::VnodeRepresentation::File{};
  return ZX_OK;
}

zx_status_t VnodeVmo::Read(void* data, size_t len, size_t off, size_t* out_actual) {
  if (off > length_) {
    *out_actual = 0;
    return ZX_OK;
  }
  size_t rlen = length_ - off;
  if (len > rlen) {
    len = rlen;
  }
  zx_status_t status = zx_vmo_read(vmo_, data, offset_ + off, len);
  if (status == ZX_OK) {
    *out_actual = len;
  }
  return status;
}

zx_status_t VnodeVmo::GetAttributes(fs::VnodeAttributes* attr) {
  *attr = fs::VnodeAttributes();
  attr->inode = ino_;
  attr->mode = V_TYPE_FILE | V_IRUSR;
  attr->content_size = length_;
  attr->storage_size = fbl::round_up(attr->content_size, GetPageSize());
  attr->link_count = link_count_;
  attr->creation_time = create_time_;
  attr->modification_time = modify_time_;
  return ZX_OK;
}

zx_status_t VnodeVmo::GetVmo(fuchsia_io::wire::VmoFlags flags, zx::vmo* out_vmo) {
  if (flags & fuchsia_io::wire::VmoFlags::kWrite) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (!have_local_clone_ && !WindowMatchesVMO(vmo_, offset_, length_)) {
    zx_status_t status = MakeLocalClone();
    if (status != ZX_OK) {
      return status;
    }
  }

  // If an exact copy is explicitly requested, but we created a local clone, e.g. because offset_
  // is non-zero, the request should fail as per fuchsia.io since it cannot be satisfied.
  if (have_local_clone_ && (flags & fuchsia_io::wire::VmoFlags::kSharedBuffer)) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Let clients map their VMOs.
  zx_rights_t rights = ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHT_GET_PROPERTY;
  rights |= (flags & fuchsia_io::wire::VmoFlags::kRead) ? ZX_RIGHT_READ : 0;
  rights |= (flags & fuchsia_io::wire::VmoFlags::kExecute) ? ZX_RIGHT_EXECUTE : 0;

  zx_handle_t vmo;
  if (flags & fuchsia_io::wire::VmoFlags::kPrivateClone) {
    // Only allow object_set_property on private VMO.
    rights |= ZX_RIGHT_SET_PROPERTY;
    // Creating a SNAPSHOT_AT_LEAST_ON_WRITE child removes ZX_RIGHT_EXECUTE even if the parent VMO
    // has it. Adding CHILD_NO_WRITE still creates a snapshot and a new VMO object, which e.g. can
    // have a unique ZX_PROP_NAME value, but the returned handle lacks WRITE and maintains EXECUTE.
    zx_status_t status = zx_vmo_create_child(
        vmo_, ZX_VMO_CHILD_SNAPSHOT_AT_LEAST_ON_WRITE | ZX_VMO_CHILD_NO_WRITE, 0, length_, &vmo);
    if (status != ZX_OK) {
      return status;
    }

    if ((status = zx_handle_replace(vmo, rights, &vmo)) != ZX_OK) {
      return status;
    }
  } else {
    zx_status_t status = zx_handle_duplicate(vmo_, rights, &vmo);
    if (status != ZX_OK) {
      return status;
    }
  }

  *out_vmo = zx::vmo(vmo);
  return ZX_OK;
}

zx_status_t VnodeVmo::MakeLocalClone() {
  // Creating a SNAPSHOT_AT_LEAST_ON_WRITE child removes ZX_RIGHT_EXECUTE even if the parent VMO has
  // it. Adding CHILD_NO_WRITE still creates a snapshot and a new VMO object, which e.g. can have a
  // unique ZX_PROP_NAME value, but the returned handle lacks WRITE and maintains EXECUTE.
  zx_handle_t tmp_vmo;
  zx_status_t status =
      zx_vmo_create_child(vmo_, ZX_VMO_CHILD_SNAPSHOT_AT_LEAST_ON_WRITE | ZX_VMO_CHILD_NO_WRITE,
                          offset_, length_, &tmp_vmo);
  if (status != ZX_OK) {
    return status;
  }

  vmo_ = tmp_vmo;
  offset_ = 0;
  have_local_clone_ = true;
  return ZX_OK;
}

}  // namespace memfs
