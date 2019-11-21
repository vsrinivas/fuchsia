// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <lib/fdio/vfs.h>
#include <lib/memfs/cpp/vnode.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zircon/device/vfs.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/ref_ptr.h>
#include <fs/vfs.h>
#include <fs/vfs_types.h>

#include "dnode.h"

namespace memfs {

// Artificially cap the maximum in-memory file size to 512MB.
constexpr size_t kMemfsMaxFileSize = 512 * 1024 * 1024;

VnodeFile::VnodeFile(Vfs* vfs) : VnodeMemfs(vfs), vmo_size_(0), length_(0) {}

VnodeFile::~VnodeFile() { vfs()->WillFreeVMO(vmo_size_); }

fs::VnodeProtocolSet VnodeFile::GetProtocols() const { return fs::VnodeProtocol::kFile; }

zx_status_t VnodeFile::Read(void* data, size_t len, size_t off, size_t* out_actual) {
  if ((off >= length_) || (!vmo_.is_valid())) {
    *out_actual = 0;
    return ZX_OK;
  } else if (len > length_ - off) {
    len = length_ - off;
  }

  zx_status_t status = vmo_.read(data, off, len);
  if (status == ZX_OK) {
    *out_actual = len;
  }
  return status;
}

zx_status_t VnodeFile::Write(const void* data, size_t len, size_t offset, size_t* out_actual) {
  zx_status_t status;
  if (offset > kMemfsMaxFileSize) {
    return ZX_ERR_FILE_BIG;
  }
  size_t newlen;
  if (add_overflow(offset, len, &newlen)) {
    return ZX_ERR_FILE_BIG;
  }
  newlen = newlen > kMemfsMaxFileSize ? kMemfsMaxFileSize : newlen;
  if ((status = vfs()->GrowVMO(vmo_, vmo_size_, newlen, &vmo_size_)) != ZX_OK) {
    return status;
  }
  // Accessing beyond the end of the file? Extend it.
  if (offset > length_) {
    // Zero-extending the tail of the file by writing to
    // an offset beyond the end of the file.
    ZeroTail(length_, offset);
  }
  size_t writelen = newlen - offset;
  if ((status = vmo_.write(data, offset, writelen)) != ZX_OK) {
    return status;
  }
  UpdateModified();
  *out_actual = writelen;

  if (newlen > length_) {
    length_ = newlen;
  }
  if (writelen < len) {
    // short write because we're beyond the end of the permissible length
    return ZX_ERR_FILE_BIG;
  }
  return ZX_OK;
}

zx_status_t VnodeFile::Append(const void* data, size_t len, size_t* out_end, size_t* out_actual) {
  zx_status_t status = Write(data, len, length_, out_actual);
  *out_end = length_;
  return status;
}

zx_status_t VnodeFile::GetVmo(int flags, zx::vmo* out_vmo, size_t* out_size) {
  zx_status_t status;
  if (!vmo_.is_valid()) {
    // First access to the file? Allocate it.
    if ((status = zx::vmo::create(0, ZX_VMO_RESIZABLE, &vmo_)) != ZX_OK) {
      return status;
    }
  }

  // Let clients map and set the names of their VMOs.
  zx_rights_t rights = ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHTS_PROPERTY;
  rights |= (flags & ::llcpp::fuchsia::io::VMO_FLAG_READ) ? ZX_RIGHT_READ : 0;
  rights |= (flags & ::llcpp::fuchsia::io::VMO_FLAG_WRITE) ? ZX_RIGHT_WRITE : 0;
  rights |= (flags & ::llcpp::fuchsia::io::VMO_FLAG_EXEC) ? ZX_RIGHT_EXECUTE : 0;
  zx::vmo result;
  if (flags & ::llcpp::fuchsia::io::VMO_FLAG_PRIVATE) {
    if ((status = vmo_.create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0, length_, &result)) != ZX_OK) {
      return status;
    }

    if ((status = result.replace(rights, &result)) != ZX_OK) {
      return status;
    }
    *out_vmo = std::move(result);
    *out_size = length_;
    return ZX_OK;
  }

  if ((status = vmo_.duplicate(rights, &result)) != ZX_OK) {
    return status;
  }
  *out_vmo = std::move(result);
  *out_size = length_;
  return ZX_OK;
}

zx_status_t VnodeFile::GetAttributes(fs::VnodeAttributes* attr) {
  *attr = fs::VnodeAttributes();
  attr->inode = ino_;
  attr->mode = V_TYPE_FILE | V_IRUSR | V_IWUSR | V_IRGRP | V_IROTH;
  attr->content_size = length_;
  attr->storage_size = fbl::round_up(attr->content_size, kMemfsBlksize);
  attr->link_count = link_count_;
  attr->creation_time = create_time_;
  attr->modification_time = modify_time_;
  return ZX_OK;
}

zx_status_t VnodeFile::GetNodeInfoForProtocol([[maybe_unused]] fs::VnodeProtocol protocol,
                                              [[maybe_unused]] fs::Rights rights,
                                              fs::VnodeRepresentation* info) {
  *info = fs::VnodeRepresentation::File();
  return ZX_OK;
}

zx_status_t VnodeFile::Truncate(size_t len) {
  zx_status_t status;
  if (len > kMemfsMaxFileSize) {
    return ZX_ERR_INVALID_ARGS;
  }
  if ((status = vfs()->GrowVMO(vmo_, vmo_size_, len, &vmo_size_)) != ZX_OK) {
    return status;
  }
  if (len < length_) {
    // Shrink the logical file length.
    // Zeroing the tail here is optional, but it saves memory.
    ZeroTail(len, length_);
  } else if (len > length_) {
    // Extend the logical file length.
    ZeroTail(length_, len);
  }

  length_ = len;
  UpdateModified();
  return ZX_OK;
}

void VnodeFile::ZeroTail(size_t start, size_t end) {
  constexpr size_t kPageSize = static_cast<size_t>(PAGE_SIZE);
  if (start % kPageSize != 0) {
    char buf[kPageSize];
    size_t ppage_size = kPageSize - (start % kPageSize);
    memset(buf, 0, ppage_size);
    ZX_ASSERT(vmo_.write(buf, start, ppage_size) == ZX_OK);
  }
  end = fbl::min(fbl::round_up(end, kPageSize), vmo_size_);
  uint64_t decommit_offset = fbl::round_up(start, kPageSize);
  uint64_t decommit_length = end - decommit_offset;

  if (decommit_length > 0) {
    ZX_ASSERT(vmo_.op_range(ZX_VMO_OP_DECOMMIT, decommit_offset, decommit_length, nullptr, 0) ==
              ZX_OK);
  }
}

}  // namespace memfs
