// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <limits.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>

#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fs/vfs.h>
#include <fs/vfs_types.h>
#include <fs/vmo_file.h>

namespace fio = ::llcpp::fuchsia::io;

namespace fs {
namespace {
constexpr uint64_t kVmoFileBlksize = PAGE_SIZE;

zx_rights_t GetVmoRightsForAccessMode(fs::Rights fs_rights) {
  zx_rights_t rights = ZX_RIGHTS_BASIC | ZX_RIGHT_MAP;
  if (fs_rights.read) {
    rights |= ZX_RIGHT_READ;
  }
  if (fs_rights.write) {
    rights |= ZX_RIGHT_WRITE;
  }
  // TODO(mdempsky): Add ZX_FS_RIGHT_EXECUTABLE flag?
  return rights;
}

}  // namespace

VmoFile::VmoFile(const zx::vmo& unowned_vmo, size_t offset, size_t length, bool writable,
                 VmoSharing vmo_sharing)
    : vmo_handle_(unowned_vmo.get()),
      offset_(offset),
      length_(length),
      writable_(writable),
      vmo_sharing_(vmo_sharing) {
  ZX_DEBUG_ASSERT(vmo_handle_ != ZX_HANDLE_INVALID);
}

VmoFile::~VmoFile() {}

VnodeProtocolSet VmoFile::GetProtocols() const { return VnodeProtocol::kMemory; }

bool VmoFile::ValidateRights(Rights rights) { return !rights.write || writable_; }

zx_status_t VmoFile::GetAttributes(VnodeAttributes* attr) {
  *attr = VnodeAttributes();
  attr->mode = V_TYPE_FILE | V_IRUSR;
  if (writable_) {
    attr->mode |= V_IWUSR;
  }
  attr->inode = fio::INO_UNKNOWN;
  attr->content_size = length_;
  attr->storage_size = fbl::round_up(attr->content_size, kVmoFileBlksize);
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
  zx_status_t status = zx_vmo_read(vmo_handle_, data, offset_ + offset, length);
  if (status != ZX_OK) {
    return status;
  }
  *out_actual = length;
  return ZX_OK;
}

zx_status_t VmoFile::Write(const void* data, size_t length, size_t offset, size_t* out_actual) {
  ZX_DEBUG_ASSERT(writable_);  // checked by the VFS

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
  zx_status_t status = zx_vmo_write(vmo_handle_, data, offset_ + offset, length);
  if (status == ZX_OK) {
    *out_actual = length;
  }
  return status;
}

zx_status_t VmoFile::GetNodeInfoForProtocol([[maybe_unused]] VnodeProtocol protocol, Rights rights,
                                            VnodeRepresentation* info) {
  ZX_DEBUG_ASSERT(!rights.write || writable_);  // checked by the VFS

  zx::vmo vmo;
  size_t offset;
  zx_status_t status = AcquireVmo(GetVmoRightsForAccessMode(rights), &vmo, &offset);
  if (status != ZX_OK) {
    return status;
  }

  *info =
      fs::VnodeRepresentation::Memory{.vmo = std::move(vmo), .offset = offset, .length = length_};
  return ZX_OK;
}

zx_status_t VmoFile::AcquireVmo(zx_rights_t rights, zx::vmo* out_vmo, size_t* out_offset) {
  ZX_DEBUG_ASSERT(!(rights & ZX_RIGHT_WRITE) || writable_);  // checked by the VFS

  switch (vmo_sharing_) {
    case VmoSharing::NONE:
      return ZX_ERR_NOT_SUPPORTED;
    case VmoSharing::DUPLICATE:
      return DuplicateVmo(rights, out_vmo, out_offset);
    case VmoSharing::CLONE_COW:
      return CloneVmo(rights, out_vmo, out_offset);
  }
  __UNREACHABLE;
}

zx_status_t VmoFile::DuplicateVmo(zx_rights_t rights, zx::vmo* out_vmo, size_t* out_offset) {
  zx_status_t status = zx_handle_duplicate(vmo_handle_, rights, out_vmo->reset_and_get_address());
  if (status != ZX_OK)
    return status;

  *out_offset = offset_;
  return ZX_OK;
}

zx_status_t VmoFile::CloneVmo(zx_rights_t rights, zx::vmo* out_vmo, size_t* out_offset) {
  size_t clone_offset = fbl::round_down(offset_, static_cast<size_t>(PAGE_SIZE));
  size_t clone_length =
      fbl::round_up(offset_ + length_, static_cast<size_t>(PAGE_SIZE)) - clone_offset;

  if (!(rights & ZX_RIGHT_WRITE)) {
    // Use a shared clone for read-only content.
    zx_status_t status = ZX_OK;
    std::call_once(shared_clone_.once, [&]() {
      status = zx_vmo_create_child(vmo_handle_, ZX_VMO_CHILD_COPY_ON_WRITE, clone_offset,
                                   clone_length, shared_clone_.vmo.reset_and_get_address());
    });
    if (status != ZX_OK)
      return status;

    status = shared_clone_.vmo.duplicate(rights, out_vmo);
    if (status != ZX_OK)
      return status;
  } else {
    // Use separate clone for each client with writable COW access.
    zx::vmo private_clone;
    zx_status_t status = zx_vmo_create_child(vmo_handle_, ZX_VMO_CHILD_COPY_ON_WRITE, clone_offset,
                                             clone_length, private_clone.reset_and_get_address());
    if (status != ZX_OK)
      return status;

    status = private_clone.replace(rights, out_vmo);
    if (status != ZX_OK)
      return status;
  }

  *out_offset = offset_ - clone_offset;
  return ZX_OK;
}

}  // namespace fs
