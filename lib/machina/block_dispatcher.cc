// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/block_dispatcher.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <block-client/client.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <lib/fdio/watcher.h>
#include <trace/event.h>
#include <virtio/virtio_ids.h>
#include <virtio/virtio_ring.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>

#include "garnet/lib/machina/phys_mem.h"
#include "garnet/lib/machina/qcow.h"
#include "garnet/lib/machina/volatile_write_block_dispatcher.h"
#include "lib/fxl/logging.h"

namespace machina {

static constexpr char kBlockDirPath[] = "/dev/class/block";

// Dispatcher that fulfills block requests using file-descriptor IO
// (ex: read/write to a file descriptor).
class FdioBlockDispatcher : public BlockDispatcher {
 public:
  FdioBlockDispatcher(size_t size, bool read_only, int fd)
      : BlockDispatcher(size, read_only), fd_(fd) {}

  zx_status_t Flush() override {
    fbl::AutoLock lock(&file_mutex_);
    return fsync(fd_) == 0 ? ZX_OK : ZX_ERR_IO;
  }

  zx_status_t Read(off_t disk_offset, void* buf, size_t size) override {
    TRACE_DURATION("machina", "io_block_read", "offset", disk_offset, "buf",
                   buf, "size", size);

    fbl::AutoLock lock(&file_mutex_);

    off_t off = lseek(fd_, disk_offset, SEEK_SET);
    if (off < 0) {
      return ZX_ERR_IO;
    }

    size_t ret = read(fd_, buf, size);
    if (ret != size) {
      return ZX_ERR_IO;
    }
    return ZX_OK;
  }

  zx_status_t Write(off_t disk_offset, const void* buf, size_t size) override {
    TRACE_DURATION("machina", "io_block_write", "offset", disk_offset, "buf",
                   buf, "size", size);

    fbl::AutoLock lock(&file_mutex_);

    off_t off = lseek(fd_, disk_offset, SEEK_SET);
    if (off < 0) {
      return ZX_ERR_IO;
    }

    size_t ret = write(fd_, buf, size);
    if (ret != size) {
      return ZX_ERR_IO;
    }
    return ZX_OK;
  }

  zx_status_t Submit() override {
    // No-op, all IO methods are synchronous.
    return ZX_OK;
  }

 private:
  fbl::Mutex file_mutex_;
  int fd_;
};

zx_status_t BlockDispatcher::CreateFromPath(
    const char* path, Mode mode, DataPlane data_plane, const PhysMem& phys_mem,
    fbl::unique_ptr<BlockDispatcher>* dispatcher) {
  bool read_only = mode == Mode::RO;
  int fd = open(path, read_only ? O_RDONLY : O_RDWR);
  if (fd < 0) {
    FXL_LOG(ERROR) << "Failed to open block file \"" << path << "\" "
                   << (read_only ? "RO" : "RW");
    return ZX_ERR_IO;
  }

  return CreateFromFd(fd, mode, data_plane, phys_mem, dispatcher);
}

struct GuidLookupArgs {
  int fd;
  BlockDispatcher::Mode mode;
  const BlockDispatcher::Guid& guid;
  ssize_t (*guid_ioctl)(int fd, void* out, size_t out_len);
};

static zx_status_t MatchBlockDeviceToGuid(int dirfd, int event, const char* fn,
                                          void* cookie) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }
  auto args = static_cast<GuidLookupArgs*>(cookie);

  fbl::unique_fd fd(openat(
      dirfd, fn, args->mode == BlockDispatcher::Mode::RO ? O_RDONLY : O_RDWR));
  if (!fd) {
    FXL_LOG(ERROR) << "Failed to open device " << kBlockDirPath << "/" << fn;
    return ZX_ERR_IO;
  }

  uint8_t device_guid[GUID_LEN];
  ssize_t result = args->guid_ioctl(fd.get(), device_guid, sizeof(device_guid));
  if (result < 0) {
    return ZX_OK;
  }
  size_t device_guid_len = static_cast<size_t>(result);
  if (args->guid.empty() || sizeof(args->guid.bytes) != device_guid_len) {
    return ZX_OK;
  }
  if (memcmp(args->guid.bytes, device_guid, device_guid_len) != 0) {
    return ZX_OK;
  }
  args->fd = fd.release();
  return ZX_ERR_STOP;
}

zx_status_t BlockDispatcher::CreateFromGuid(
    const Guid& guid, zx_duration_t timeout, Mode mode, DataPlane data_plane,
    const PhysMem& phys_mem, fbl::unique_ptr<BlockDispatcher>* dispatcher) {
  GuidLookupArgs args = {-1, mode, guid, nullptr};
  switch (guid.type) {
    case GuidType::GPT_PARTITION_GUID:
      args.guid_ioctl = &ioctl_block_get_partition_guid;
      break;
    case GuidType::GPT_PARTITION_TYPE_GUID:
      args.guid_ioctl = &ioctl_block_get_type_guid;
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }

  fbl::unique_fd dir_fd(open(kBlockDirPath, O_DIRECTORY | O_RDONLY));
  if (!dir_fd) {
    return ZX_ERR_IO;
  }

  zx_status_t status = fdio_watch_directory(
      dir_fd.get(), MatchBlockDeviceToGuid, timeout, &args);
  if (status == ZX_ERR_STOP) {
    return CreateFromFd(args.fd, mode, data_plane, phys_mem, dispatcher);
  }
  return status;
}

zx_status_t BlockDispatcher::CreateFromFd(
    int fd, Mode mode, DataPlane data_plane, const PhysMem& phys_mem,
    fbl::unique_ptr<BlockDispatcher>* dispatcher) {
  off_t file_size = lseek(fd, 0, SEEK_END);
  if (file_size < 0) {
    FXL_LOG(ERROR) << "Failed to read size of block device";
    return ZX_ERR_IO;
  }

  bool read_only = mode == Mode::RO;
  switch (data_plane) {
    case DataPlane::FDIO:
      *dispatcher =
          fbl::make_unique<FdioBlockDispatcher>(file_size, read_only, fd);
      return ZX_OK;
    case DataPlane::QCOW:
      return QcowDispatcher::Create(fd, read_only, dispatcher);
    default:
      FXL_LOG(ERROR) << "Unsupported block dispatcher data plane";
      return ZX_ERR_INVALID_ARGS;
  }
}

zx_status_t BlockDispatcher::CreateVolatileWrapper(
    fbl::unique_ptr<BlockDispatcher> dispatcher,
    fbl::unique_ptr<BlockDispatcher>* out) {
  return VolatileWriteBlockDispatcher::Create(std::move(dispatcher), out);
}

}  // namespace machina
