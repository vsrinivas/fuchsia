// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/security/zxcrypt/fdio-volume.h"

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.hardware.block.encrypted/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <inttypes.h>
#include <lib/component/cpp/incoming/service_client.h>
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

FdioVolume::FdioVolume(fbl::unique_fd&& block_dev_fd) : block_dev_fd_(std::move(block_dev_fd)) {}

zx_status_t FdioVolume::Init(fbl::unique_fd block_dev_fd, std::unique_ptr<FdioVolume>* out) {
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

  if (zx_status_t status = volume->Init(); status != ZX_OK) {
    return status;
  }

  *out = std::move(volume);
  return ZX_OK;
}

zx_status_t FdioVolume::Create(fbl::unique_fd block_dev_fd, const crypto::Secret& key,
                               std::unique_ptr<FdioVolume>* out) {
  std::unique_ptr<FdioVolume> volume;

  if (zx_status_t status = FdioVolume::Init(std::move(block_dev_fd), &volume); status != ZX_OK) {
    xprintf("Init failed: %s\n", zx_status_get_string(status));
    return status;
  }

  uint8_t slot = 0;
  if (zx_status_t status = volume->Format(key, slot); status != ZX_OK) {
    xprintf("Format failed: %s\n", zx_status_get_string(status));
    return status;
  }

  if (out) {
    *out = std::move(volume);
  }
  return ZX_OK;
}

zx_status_t FdioVolume::Unlock(fbl::unique_fd block_dev_fd, const crypto::Secret& key,
                               key_slot_t slot, std::unique_ptr<FdioVolume>* out) {
  std::unique_ptr<FdioVolume> volume;
  if (zx_status_t status = FdioVolume::Init(std::move(block_dev_fd), &volume); status != ZX_OK) {
    xprintf("Init failed: %s\n", zx_status_get_string(status));
    return status;
  }
  if (zx_status_t status = volume->Unlock(key, slot); status != ZX_OK) {
    xprintf("Unlock failed: %s\n", zx_status_get_string(status));
    return status;
  }

  *out = std::move(volume);
  return ZX_OK;
}

zx_status_t FdioVolume::Unlock(const crypto::Secret& key, key_slot_t slot) {
  return Volume::Unlock(key, slot);
}

// Configuration methods
zx_status_t FdioVolume::Enroll(const crypto::Secret& key, key_slot_t slot) {
  if (zx_status_t status = SealBlock(key, slot); status != ZX_OK) {
    xprintf("SealBlock failed: %s\n", zx_status_get_string(status));
    return status;
  }
  if (zx_status_t status = CommitBlock(); status != ZX_OK) {
    xprintf("CommitBlock failed: %s\n", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

zx_status_t FdioVolume::Revoke(key_slot_t slot) {
  zx_off_t off;
  crypto::Bytes invalid;
  if (zx_status_t status = GetSlotOffset(slot, &off); status != ZX_OK) {
    xprintf("GetSlotOffset failed: %s\n", zx_status_get_string(status));
    return status;
  }
  if (zx_status_t status = invalid.Randomize(slot_len_); status != ZX_OK) {
    xprintf("Randomize failed: %s\n", zx_status_get_string(status));
    return status;
  }
  if (zx_status_t status = block_.Copy(invalid, off); status != ZX_OK) {
    xprintf("Copy failed: %s\n", zx_status_get_string(status));
    return status;
  }
  if (zx_status_t status = CommitBlock(); status != ZX_OK) {
    xprintf("CommitBlock failed: %s\n", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

zx_status_t FdioVolume::Init() { return Volume::Init(); }

zx_status_t FdioVolume::GetBlockInfo(BlockInfo* out) {
  fdio_cpp::UnownedFdioCaller caller(block_dev_fd_.get());
  if (!caller) {
    return ZX_ERR_BAD_STATE;
  }
  const fidl::WireResult result =
      fidl::WireCall(caller.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
  if (!result.ok()) {
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  if (response.status != ZX_OK) {
    return response.status;
  }

  out->block_count = response.info->block_count;
  out->block_size = response.info->block_size;
  return ZX_OK;
}

zx_status_t FdioVolume::GetFvmSliceSize(uint64_t* out) {
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
  //
  // TODO(https://fxbug.dev/112484): this relies on multiplexing.
  //
  // TODO(https://fxbug.dev/113512): Remove this.
  zx::result cloned = component::Clone(caller.borrow_as<fuchsia_hardware_block_volume::Volume>(),
                                       component::AssumeProtocolComposesNode);
  if (cloned.is_error()) {
    return cloned.status_value();
  }
  const fidl::WireResult result = fidl::WireCall(cloned.value())->GetVolumeInfo();
  if (!result.ok()) {
    if (result.is_peer_closed()) {
      // The channel being closed here means that the thing at the other
      // end of this channel does not speak the FVM protocol, and has
      // closed the channel on us.  Return the appropriate error to signal
      // that we shouldn't bother with any of the FVM codepaths.
      return ZX_ERR_NOT_SUPPORTED;
    }
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  if (response.status != ZX_OK) {
    return response.status;
  }

  *out = response.manager->slice_size;
  return ZX_OK;
}

zx_status_t FdioVolume::DoBlockFvmVsliceQuery(uint64_t vslice_start,
                                              SliceRegion ranges[Volume::MAX_SLICE_REGIONS],
                                              uint64_t* slice_count) {
  static_assert(fuchsia_hardware_block_volume::wire::kMaxSliceRequests == Volume::MAX_SLICE_REGIONS,
                "block volume slice response count must match");
  fdio_cpp::UnownedFdioCaller caller(block_dev_fd_.get());
  if (!caller) {
    return ZX_ERR_BAD_STATE;
  }

  const fidl::WireResult result =
      fidl::WireCall(caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
          ->QuerySlices(fidl::VectorView<uint64_t>::FromExternal(&vslice_start, 1));
  if (!result.ok()) {
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  if (response.status != ZX_OK) {
    return response.status;
  }

  if (response.response_count > Volume::MAX_SLICE_REGIONS) {
    // Should be impossible.  Trust nothing.
    return ZX_ERR_BAD_STATE;
  }

  *slice_count = response.response_count;
  for (size_t i = 0; i < response.response_count; i++) {
    ranges[i].allocated = response.response[i].allocated;
    ranges[i].count = response.response[i].count;
  }

  return ZX_OK;
}

zx_status_t FdioVolume::DoBlockFvmExtend(uint64_t start_slice, uint64_t slice_count) {
  fdio_cpp::UnownedFdioCaller caller(block_dev_fd_.get());
  if (!caller) {
    return ZX_ERR_BAD_STATE;
  }
  const fidl::WireResult result =
      fidl::WireCall(caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
          ->Extend(start_slice, slice_count);
  if (!result.ok()) {
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  if (response.status != ZX_OK) {
    return response.status;
  }

  return ZX_OK;
}

zx_status_t FdioVolume::Read() {
  if (lseek(block_dev_fd_.get(), offset_, SEEK_SET) < 0) {
    xprintf("lseek(%d, %" PRIu64 ", SEEK_SET) failed: %s\n", block_dev_fd_.get(), offset_,
            strerror(errno));
    return ZX_ERR_IO;
  }
  ssize_t res = res = read(block_dev_fd_.get(), block_.get(), block_.len());
  if (res < 0) {
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
  ssize_t res = write(block_dev_fd_.get(), block_.get(), block_.len());
  if (res < 0) {
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

zx_status_t FdioVolume::Flush() {
  // On Fuchsia, an FD produced by opening a block device out of the device tree doesn't implement
  // fsync(), so we stub this out.  FdioVolume is only used for tests anyway, which don't need to
  // worry too much about durability.
  return ZX_OK;
}

}  // namespace zxcrypt
