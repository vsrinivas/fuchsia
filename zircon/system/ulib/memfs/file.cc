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

#include <algorithm>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/ref_ptr.h>
#include <fs/vfs.h>
#include <fs/vfs_types.h>

#include "dnode.h"

namespace memfs {

// Artificially cap the maximum in-memory file size to 512MB.
constexpr size_t kMemfsMaxFileSize = 512 * 1024 * 1024;

VnodeFile::VnodeFile(Vfs* vfs) : VnodeMemfs(vfs) {}

VnodeFile::~VnodeFile() = default;

fs::VnodeProtocolSet VnodeFile::GetProtocols() const { return fs::VnodeProtocol::kFile; }

zx_status_t VnodeFile::CreateStream(uint32_t stream_options, zx::stream* out_stream) {
  zx_status_t status = CreateBackingStoreIfNeeded();
  if (status != ZX_OK) {
    return status;
  }
  return zx::stream::create(stream_options, vmo_, 0u, out_stream);
}

void VnodeFile::DidModifyStream() { UpdateModified(); }

zx_status_t VnodeFile::GetVmo(int flags, zx::vmo* out_vmo, size_t* out_size) {
  zx_status_t status = CreateBackingStoreIfNeeded();
  if (status != ZX_OK) {
    return status;
  }
  size_t content_size = GetContentSize();
  // Let clients map and set the names of their VMOs.
  zx_rights_t rights = ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHT_GET_PROPERTY;
  rights |= (flags & ::llcpp::fuchsia::io::VMO_FLAG_READ) ? ZX_RIGHT_READ : 0;
  rights |= (flags & ::llcpp::fuchsia::io::VMO_FLAG_WRITE) ? ZX_RIGHT_WRITE : 0;
  rights |= (flags & ::llcpp::fuchsia::io::VMO_FLAG_EXEC) ? ZX_RIGHT_EXECUTE : 0;
  zx::vmo result;
  if (flags & ::llcpp::fuchsia::io::VMO_FLAG_PRIVATE) {
    rights |= ZX_RIGHT_SET_PROPERTY;  // Only allow object_set_property on private VMO.
    if ((status = vmo_.create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0, content_size, &result)) !=
        ZX_OK) {
      return status;
    }

    if ((status = result.replace(rights, &result)) != ZX_OK) {
      return status;
    }
  } else {
    if ((status = vmo_.duplicate(rights, &result)) != ZX_OK) {
      return status;
    }
  }

  *out_vmo = std::move(result);
  *out_size = content_size;
  return ZX_OK;
}

zx_status_t VnodeFile::GetAttributes(fs::VnodeAttributes* attr) {
  *attr = fs::VnodeAttributes();
  attr->inode = ino_;
  attr->mode = V_TYPE_FILE | V_IRUSR | V_IWUSR | V_IRGRP | V_IROTH;
  attr->content_size = GetContentSize();
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

zx_status_t VnodeFile::Truncate(size_t length) {
  if (length > kMemfsMaxFileSize) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_status_t status = CreateBackingStoreIfNeeded();
  if (status != ZX_OK) {
    return status;
  }

  // TODO: When we give clients direct access to a zx::stream, we will expose a race condition
  // between these two lines. Suppose an append happens between these two statements and we are
  // growing the size of the file. The previous_content_size value will be stale, which means we
  // will clobber some of the appended data when we ZeroTail below. We might need to move the
  // truncate operation into the kernel in order to be sufficiently atomic.
  size_t previous_content_size = GetContentSize();
  vmo_.set_property(ZX_PROP_VMO_CONTENT_SIZE, &length, sizeof(length));

  if (length < previous_content_size) {
    // Shrink the logical file length.
    // Zeroing the tail here is optional, but it saves memory.
    ZeroTail(length, previous_content_size);
  } else if (length > previous_content_size) {
    // Extend the logical file length.
    ZeroTail(previous_content_size, length);
  }

  UpdateModified();
  return ZX_OK;
}

zx_status_t VnodeFile::CreateBackingStoreIfNeeded() {
  if (vmo_.is_valid()) {
    return ZX_OK;
  }
  return zx::vmo::create(kMemfsMaxFileSize, 0, &vmo_);
}

size_t VnodeFile::GetContentSize() const {
  if (!vmo_.is_valid()) {
    return 0u;
  }
  size_t content_size = 0u;
  zx_status_t status =
      vmo_.get_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size));
  if (status != ZX_OK) {
    return 0u;
  }
  return content_size;
}

void VnodeFile::ZeroTail(size_t start, size_t end) {
  constexpr size_t kPageSize = static_cast<size_t>(PAGE_SIZE);
  if (start % kPageSize != 0) {
    char buf[kPageSize];
    size_t ppage_size = kPageSize - (start % kPageSize);
    memset(buf, 0, ppage_size);
    ZX_ASSERT(vmo_.write(buf, start, ppage_size) == ZX_OK);
  }
  end = std::min(fbl::round_up(end, kPageSize), kMemfsMaxFileSize);
  uint64_t decommit_offset = fbl::round_up(start, kPageSize);
  uint64_t decommit_length = end - decommit_offset;

  if (decommit_length > 0) {
    ZX_ASSERT(vmo_.op_range(ZX_VMO_OP_DECOMMIT, decommit_offset, decommit_length, nullptr, 0) ==
              ZX_OK);
  }
}

}  // namespace memfs
