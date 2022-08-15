// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/vfs/cpp/vmo_file.h>

namespace vfs {

VmoFile::VmoFile(zx::vmo vmo, size_t length, WriteOption write_option, Sharing vmo_sharing)
    : length_(length),
      write_option_(write_option),
      vmo_sharing_(vmo_sharing),
      vmo_(std::move(vmo)) {}

VmoFile::~VmoFile() = default;

zx_status_t VmoFile::GetBackingMemory(fuchsia::io::VmoFlags flags, zx::vmo* out_vmo) {
  zx_rights_t rights = ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHT_GET_PROPERTY;
  if ((flags & fuchsia::io::VmoFlags::READ) != fuchsia::io::VmoFlags{}) {
    rights |= ZX_RIGHT_READ;
  }
  if ((flags & fuchsia::io::VmoFlags::WRITE) != fuchsia::io::VmoFlags{}) {
    rights |= ZX_RIGHT_WRITE | ZX_RIGHT_SET_PROPERTY;
  }
  if ((flags & fuchsia::io::VmoFlags::PRIVATE_CLONE) != fuchsia::io::VmoFlags{}) {
    zx::vmo vmo;
    zx_status_t status =
        vmo_.create_child(ZX_VMO_CHILD_SNAPSHOT_AT_LEAST_ON_WRITE, 0, length_, &vmo);
    if (status != ZX_OK) {
      return status;
    }
    return vmo.replace(rights, out_vmo);
  }
  if ((flags & fuchsia::io::VmoFlags::SHARED_BUFFER) != fuchsia::io::VmoFlags{}) {
    return vmo_.duplicate(rights, out_vmo);
  }
  switch (vmo_sharing_) {
    case Sharing::NONE:
      return ZX_ERR_NOT_SUPPORTED;
    case Sharing::DUPLICATE:
      return vmo_.duplicate(rights, out_vmo);
    case Sharing::CLONE_COW:
      return vmo_.create_child(ZX_VMO_CHILD_SNAPSHOT_AT_LEAST_ON_WRITE, 0, length_, out_vmo);
  }
}

zx_status_t VmoFile::ReadAt(uint64_t count, uint64_t offset, std::vector<uint8_t>* out_data) {
  if (count == 0u || offset >= length_) {
    return ZX_OK;
  }

  size_t remaining_length = length_ - offset;
  if (count > remaining_length) {
    count = remaining_length;
  }

  out_data->resize(count);
  return vmo_.read(out_data->data(), offset, count);
}

zx_status_t VmoFile::WriteAt(std::vector<uint8_t> data, uint64_t offset, uint64_t* out_actual) {
  if (write_option_ != WriteOption::WRITABLE) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  size_t length = data.size();
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
  zx_status_t status = vmo_.write(data.data(), offset, length);
  if (status == ZX_OK) {
    *out_actual = length;
  }
  return status;
}

zx_status_t VmoFile::Truncate(uint64_t length) { return ZX_ERR_NOT_SUPPORTED; }

size_t VmoFile::GetCapacity() { return length_; }

size_t VmoFile::GetLength() { return length_; }

zx_status_t VmoFile::GetAttr(fuchsia::io::NodeAttributes* out_attributes) const {
  fuchsia::io::OpenFlags flags = fuchsia::io::OpenFlags::RIGHT_READABLE;
  if (write_option_ == WriteOption::WRITABLE) {
    flags |= fuchsia::io::OpenFlags::RIGHT_WRITABLE;
  }
  out_attributes->mode = fuchsia::io::MODE_TYPE_FILE | static_cast<uint32_t>(flags);
  out_attributes->id = fuchsia::io::INO_UNKNOWN;
  out_attributes->content_size = length_;
  out_attributes->storage_size = length_;
  out_attributes->link_count = 1;
  out_attributes->creation_time = 0;
  out_attributes->modification_time = 0;
  return ZX_OK;
}

fuchsia::io::OpenFlags VmoFile::GetAllowedFlags() const {
  fuchsia::io::OpenFlags flags = File::GetAllowedFlags() | fuchsia::io::OpenFlags::RIGHT_READABLE;
  if (write_option_ == WriteOption::WRITABLE) {
    flags |= fuchsia::io::OpenFlags::RIGHT_WRITABLE;
  }
  return flags;
}

}  // namespace vfs
