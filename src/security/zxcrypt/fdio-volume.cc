// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/security/zxcrypt/fdio-volume.h"

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/encrypted/c/fidl.h>
#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <inttypes.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/zircon-internal/debug.h>
#include <lib/zx/channel.h>
#include <unistd.h>
#include <zircon/status.h>

#include <memory>
#include <utility>

#include <fbl/string_buffer.h>
#include <fbl/vector.h>

#include "src/security/zxcrypt/volume.h"

#define ZXDEBUG 0

namespace zxcrypt {

FdioVolume::FdioVolume(fbl::unique_fd&& block_dev_fd)
    : Volume(), block_dev_fd_(std::move(block_dev_fd)) {}

zx_status_t FdioVolume::Init(fbl::unique_fd block_dev_fd, std::unique_ptr<FdioVolume>* out) {
  zx_status_t rc;

  if (!block_dev_fd || !out) {
    xprintf("bad parameter(s): block_dev_fd=%d, out=%p\n", block_dev_fd.get(), out);
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<FdioVolume> volume(new (&ac) FdioVolume(std::move(block_dev_fd)));
  if (!ac.check()) {
    xprintf("allocation failed: %zu bytes\n", sizeof(FdioVolume));
    return ZX_ERR_NO_MEMORY;
  }

  if ((rc = volume->Init()) != ZX_OK) {
    return rc;
  }

  *out = std::move(volume);
  return ZX_OK;
}

zx_status_t FdioVolume::Create(fbl::unique_fd block_dev_fd, const crypto::Secret& key,
                               std::unique_ptr<FdioVolume>* out) {
  zx_status_t rc;

  std::unique_ptr<FdioVolume> volume;

  if ((rc = FdioVolume::Init(std::move(block_dev_fd), &volume)) != ZX_OK) {
    xprintf("Init failed: %s\n", zx_status_get_string(rc));
    return rc;
  }

  uint8_t slot = 0;
  if ((rc = volume->Format(key, slot)) != ZX_OK) {
    xprintf("Format failed: %s\n", zx_status_get_string(rc));
    return rc;
  }

  if (out) {
    *out = std::move(volume);
  }
  return ZX_OK;
}

zx_status_t FdioVolume::Unlock(fbl::unique_fd block_dev_fd, const crypto::Secret& key,
                               key_slot_t slot, std::unique_ptr<FdioVolume>* out) {
  zx_status_t rc;

  std::unique_ptr<FdioVolume> volume;
  if ((rc = FdioVolume::Init(std::move(block_dev_fd), &volume)) != ZX_OK) {
    xprintf("Init failed: %s\n", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = volume->Unlock(key, slot)) != ZX_OK) {
    xprintf("Unlock failed: %s\n", zx_status_get_string(rc));
    return rc;
  }

  *out = std::move(volume);
  return ZX_OK;
}

zx_status_t FdioVolume::Unlock(const crypto::Secret& key, key_slot_t slot) {
  return Volume::Unlock(key, slot);
}

// Configuration methods
zx_status_t FdioVolume::Enroll(const crypto::Secret& key, key_slot_t slot) {
  zx_status_t rc;

  if ((rc = SealBlock(key, slot)) != ZX_OK) {
    xprintf("SealBlock failed: %s\n", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = CommitBlock()) != ZX_OK) {
    xprintf("CommitBlock failed: %s\n", zx_status_get_string(rc));
    return rc;
  }

  return ZX_OK;
}

zx_status_t FdioVolume::Revoke(key_slot_t slot) {
  zx_status_t rc;

  zx_off_t off;
  crypto::Bytes invalid;
  if ((rc = GetSlotOffset(slot, &off)) != ZX_OK) {
    xprintf("GetSlotOffset failed: %s\n", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = invalid.Randomize(slot_len_)) != ZX_OK) {
    xprintf("Randomize failed: %s\n", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = block_.Copy(invalid, off)) != ZX_OK) {
    xprintf("Copy failed: %s\n", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = CommitBlock()) != ZX_OK) {
    xprintf("CommitBlock failed: %s\n", zx_status_get_string(rc));
    return rc;
  }

  return ZX_OK;
}

zx_status_t FdioVolume::Init() { return Volume::Init(); }

zx_status_t FdioVolume::GetBlockInfo(BlockInfo* out) {
  zx_status_t rc;
  zx_status_t call_status;
  fdio_cpp::UnownedFdioCaller caller(block_dev_fd_.get());
  if (!caller) {
    return ZX_ERR_BAD_STATE;
  }
  fuchsia_hardware_block_BlockInfo block_info;
  if ((rc = fuchsia_hardware_block_BlockGetInfo(caller.borrow_channel(), &call_status,
                                                &block_info)) != ZX_OK) {
    return rc;
  }
  if (call_status != ZX_OK) {
    return call_status;
  }

  out->block_count = block_info.block_count;
  out->block_size = block_info.block_size;
  return ZX_OK;
}

zx_status_t FdioVolume::GetFvmSliceSize(uint64_t* out) {
  zx_status_t rc;
  zx_status_t call_status;
  fdio_cpp::UnownedFdioCaller caller(block_dev_fd_.get());
  if (!caller) {
    return ZX_ERR_BAD_STATE;
  }

  // When this function is called, we're not yet sure if the underlying device
  // actually implements the block protocol, and we use the return value here
  // to tell us if we should utilize FVM-specific codepaths or not.
  // If the underlying channel doesn't respond to volume methods, when we call
  // a method from fuchsia.hardware.block.volume the FIDL channel will be
  // closed and we'll be unable to do other calls to it.  So before making
  // this call, we clone the channel.
  zx::channel channel(fdio_service_clone(caller.borrow_channel()));

  fuchsia_hardware_block_volume_VolumeInfo volume_info;
  if ((rc = fuchsia_hardware_block_volume_VolumeQuery(channel.get(), &call_status, &volume_info)) !=
      ZX_OK) {
    if (rc == ZX_ERR_PEER_CLOSED) {
      // The channel being closed here means that the thing at the other
      // end of this channel does not speak the FVM protocol, and has
      // closed the channel on us.  Return the appropriate error to signal
      // that we shouldn't bother with any of the FVM codepaths.
      return ZX_ERR_NOT_SUPPORTED;
    }
    return rc;
  }
  if (call_status != ZX_OK) {
    return call_status;
  }

  *out = volume_info.slice_size;
  return ZX_OK;
}

zx_status_t FdioVolume::DoBlockFvmVsliceQuery(uint64_t vslice_start,
                                              SliceRegion ranges[Volume::MAX_SLICE_REGIONS],
                                              uint64_t* slice_count) {
  static_assert(fuchsia_hardware_block_volume_MAX_SLICE_REQUESTS == Volume::MAX_SLICE_REGIONS,
                "block volume slice response count must match");
  zx_status_t rc;
  zx_status_t call_status;
  fdio_cpp::UnownedFdioCaller caller(block_dev_fd_.get());
  if (!caller) {
    return ZX_ERR_BAD_STATE;
  }
  fuchsia_hardware_block_volume_VsliceRange tmp_ranges[Volume::MAX_SLICE_REGIONS];
  uint64_t range_count;

  if ((rc = fuchsia_hardware_block_volume_VolumeQuerySlices(
           caller.borrow_channel(), &vslice_start, 1, &call_status, tmp_ranges, &range_count)) !=
      ZX_OK) {
    return rc;
  }
  if (call_status != ZX_OK) {
    return call_status;
  }

  if (range_count > Volume::MAX_SLICE_REGIONS) {
    // Should be impossible.  Trust nothing.
    return ZX_ERR_BAD_STATE;
  }

  *slice_count = range_count;
  for (size_t i = 0; i < range_count; i++) {
    ranges[i].allocated = tmp_ranges[i].allocated;
    ranges[i].count = tmp_ranges[i].count;
  }

  return ZX_OK;
}

zx_status_t FdioVolume::DoBlockFvmExtend(uint64_t start_slice, uint64_t slice_count) {
  zx_status_t rc;
  zx_status_t call_status;
  fdio_cpp::UnownedFdioCaller caller(block_dev_fd_.get());
  if (!caller) {
    return ZX_ERR_BAD_STATE;
  }
  if ((rc = fuchsia_hardware_block_volume_VolumeExtend(caller.borrow_channel(), start_slice,
                                                       slice_count, &call_status)) != ZX_OK) {
    return rc;
  }
  if (call_status != ZX_OK) {
    return call_status;
  }

  return ZX_OK;
}

zx_status_t FdioVolume::Read() {
  if (lseek(block_dev_fd_.get(), offset_, SEEK_SET) < 0) {
    xprintf("lseek(%d, %" PRIu64 ", SEEK_SET) failed: %s\n", block_dev_fd_.get(), offset_,
            strerror(errno));
    return ZX_ERR_IO;
  }
  ssize_t res;
  if ((res = read(block_dev_fd_.get(), block_.get(), block_.len())) < 0) {
    xprintf("read(%d, %p, %zu) failed: %s\n", block_dev_fd_.get(), block_.get(), block_.len(),
            strerror(errno));
    return ZX_ERR_IO;
  }
  if (static_cast<size_t>(res) != block_.len()) {
    xprintf("short read: have %zd, need %zu\n", res, block_.len());
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

zx_status_t FdioVolume::Write() {
  if (lseek(block_dev_fd_.get(), offset_, SEEK_SET) < 0) {
    xprintf("lseek(%d, %" PRIu64 ", SEEK_SET) failed: %s\n", block_dev_fd_.get(), offset_,
            strerror(errno));
    return ZX_ERR_IO;
  }
  ssize_t res;
  if ((res = write(block_dev_fd_.get(), block_.get(), block_.len())) < 0) {
    xprintf("write(%d, %p, %zu) failed: %s\n", block_dev_fd_.get(), block_.get(), block_.len(),
            strerror(errno));
    return ZX_ERR_IO;
  }
  if (static_cast<size_t>(res) != block_.len()) {
    xprintf("short write: have %zd, need %zu\n", res, block_.len());
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

}  // namespace zxcrypt
