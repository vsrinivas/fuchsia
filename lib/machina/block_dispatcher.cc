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
#include <fdio/watcher.h>
#include <virtio/virtio_ids.h>
#include <virtio/virtio_ring.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>

#include "lib/fxl/logging.h"

namespace machina {

static const char kBlockDirPath[] = "/dev/class/block";

// Dispatcher that fulfills block requests using file-descriptor IO
// (ex: read/write to a file descriptor).
class FdioBlockDispatcher : public BlockDispatcher {
 public:
  static zx_status_t Create(int fd,
                            size_t size,
                            bool read_only,
                            fbl::unique_ptr<BlockDispatcher>* out) {
    fbl::AllocChecker ac;
    auto dispatcher =
        fbl::make_unique_checked<FdioBlockDispatcher>(&ac, size, read_only, fd);
    if (!ac.check())
      return ZX_ERR_NO_MEMORY;

    *out = fbl::move(dispatcher);
    return ZX_OK;
  }

  FdioBlockDispatcher(size_t size, bool read_only, int fd)
      : BlockDispatcher(size, read_only), fd_(fd) {}

  zx_status_t Flush() override {
    fbl::AutoLock lock(&file_mutex_);
    return fsync(fd_) == 0 ? ZX_OK : ZX_ERR_IO;
  }

  zx_status_t Read(off_t disk_offset, void* buf, size_t size) override {
    fbl::AutoLock lock(&file_mutex_);
    off_t off = lseek(fd_, disk_offset, SEEK_SET);
    if (off < 0)
      return ZX_ERR_IO;

    size_t ret = read(fd_, buf, size);
    if (ret != size)
      return ZX_ERR_IO;
    return ZX_OK;
  }

  zx_status_t Write(off_t disk_offset, const void* buf, size_t size) override {
    fbl::AutoLock lock(&file_mutex_);
    off_t off = lseek(fd_, disk_offset, SEEK_SET);
    if (off < 0)
      return ZX_ERR_IO;

    size_t ret = write(fd_, buf, size);
    if (ret != size)
      return ZX_ERR_IO;
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

class FifoBlockDispatcher : public BlockDispatcher {
 public:
  static zx_status_t Create(int fd,
                            size_t size,
                            bool read_only,
                            const PhysMem& phys_mem,
                            fbl::unique_ptr<BlockDispatcher>* out) {
    zx_handle_t fifo;
    ssize_t result = ioctl_block_get_fifos(fd, &fifo);
    if (result != sizeof(fifo))
      return ZX_ERR_IO;
    auto close_fifo = fbl::MakeAutoCall([fifo]() { zx_handle_close(fifo); });

    txnid_t txnid = TXNID_INVALID;
    result = ioctl_block_alloc_txn(fd, &txnid);
    if (result != sizeof(txnid_))
      return ZX_ERR_IO;
    auto free_txn =
        fbl::MakeAutoCall([fd, txnid]() { ioctl_block_free_txn(fd, &txnid); });

    zx_handle_t vmo_dup;
    zx_status_t status =
        zx_handle_duplicate(phys_mem.vmo(), ZX_RIGHT_SAME_RIGHTS, &vmo_dup);
    if (status != ZX_OK)
      return ZX_ERR_IO;

    // TODO(ZX-1333): Limit how much of they guest physical address space
    // is exposed to the block server.
    vmoid_t vmoid;
    result = ioctl_block_attach_vmo(fd, &vmo_dup, &vmoid);
    if (result != sizeof(vmoid_)) {
      zx_handle_close(vmo_dup);
      return ZX_ERR_IO;
    }

    fifo_client_t* fifo_client = nullptr;
    status = block_fifo_create_client(fifo, &fifo_client);
    if (status != ZX_OK)
      return ZX_ERR_IO;

    // The fifo handle is now owned by the block client.
    fifo = ZX_HANDLE_INVALID;
    auto free_fifo_client = fbl::MakeAutoCall(
        [fifo_client]() { block_fifo_release_client(fifo_client); });

    fbl::AllocChecker ac;
    auto dispatcher = fbl::make_unique_checked<FifoBlockDispatcher>(
        &ac, size, read_only, fd, txnid, vmoid, fifo_client, phys_mem.addr());
    if (!ac.check())
      return ZX_ERR_NO_MEMORY;

    close_fifo.cancel();
    free_txn.cancel();
    free_fifo_client.cancel();
    *out = fbl::move(dispatcher);
    return ZX_OK;
  }

  FifoBlockDispatcher(size_t size,
                      bool read_only,
                      int fd,
                      txnid_t txnid,
                      vmoid_t vmoid,
                      fifo_client_t* fifo_client,
                      size_t guest_vmo_addr)
      : BlockDispatcher(size, read_only),
        fd_(fd),
        txnid_(txnid),
        vmoid_(vmoid),
        fifo_client_(fifo_client),
        guest_vmo_addr_(guest_vmo_addr) {}

  ~FifoBlockDispatcher() {
    if (txnid_ != TXNID_INVALID) {
      ioctl_block_free_txn(fd_, &txnid_);
    }
    if (fifo_client_ != nullptr) {
      block_fifo_release_client(fifo_client_);
    }
  }

  zx_status_t Flush() override { return ZX_OK; }

  zx_status_t Read(off_t disk_offset, void* buf, size_t size) override {
    fbl::AutoLock lock(&fifo_mutex_);
    return EnqueueBlockRequestLocked(BLOCKIO_READ, disk_offset, buf, size);
  }

  zx_status_t Write(off_t disk_offset, const void* buf, size_t size) override {
    fbl::AutoLock lock(&fifo_mutex_);
    return EnqueueBlockRequestLocked(BLOCKIO_WRITE, disk_offset, buf, size);
  }

  zx_status_t Submit() override {
    fbl::AutoLock lock(&fifo_mutex_);
    return SubmitTransactionsLocked();
  }

 private:
  zx_status_t EnqueueBlockRequestLocked(uint16_t opcode,
                                        off_t disk_offset,
                                        const void* buf,
                                        size_t size)
      __TA_REQUIRES(fifo_mutex_) {
    if (request_index_ >= kNumRequests) {
      zx_status_t status = SubmitTransactionsLocked();
      if (status != ZX_OK)
        return status;
    }

    block_fifo_request_t* request = &requests_[request_index_++];
    request->txnid = txnid_;
    request->vmoid = vmoid_;
    request->opcode = opcode;
    request->length = size;
    request->vmo_offset = reinterpret_cast<uint64_t>(buf) - guest_vmo_addr_;
    request->dev_offset = disk_offset;
    return ZX_OK;
  }

  zx_status_t SubmitTransactionsLocked() __TA_REQUIRES(fifo_mutex_) {
    zx_status_t status =
        block_fifo_txn(fifo_client_, requests_, request_index_);
    request_index_ = 0;
    return status;
  }

  // Block server access.
  int fd_;
  txnid_t txnid_ = TXNID_INVALID;
  vmoid_t vmoid_;
  fifo_client_t* fifo_client_ = nullptr;

  size_t guest_vmo_addr_;
  size_t request_index_ __TA_GUARDED(fifo_mutex_) = 0;
  static constexpr size_t kNumRequests = MAX_TXN_MESSAGES;
  block_fifo_request_t requests_[kNumRequests] __TA_GUARDED(fifo_mutex_);
  fbl::Mutex fifo_mutex_;
};

zx_status_t BlockDispatcher::CreateFromPath(
    const char* path,
    Mode mode,
    DataPlane data_plane,
    const PhysMem& phys_mem,
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

static zx_status_t MatchBlockDeviceToGuid(int dirfd,
                                          int event,
                                          const char* fn,
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
    const Guid& guid,
    zx_duration_t timeout,
    Mode mode,
    DataPlane data_plane,
    const PhysMem& phys_mem,
    fbl::unique_ptr<BlockDispatcher>* dispatcher) {
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
    int fd,
    Mode mode,
    DataPlane data_plane,
    const PhysMem& phys_mem,
    fbl::unique_ptr<BlockDispatcher>* dispatcher) {
  off_t file_size = lseek(fd, 0, SEEK_END);
  if (file_size < 0) {
    FXL_LOG(ERROR) << "Failed to read size of block device";
    return ZX_ERR_IO;
  }

  bool read_only = mode == Mode::RO;
  switch (data_plane) {
    case DataPlane::FDIO:
      return FdioBlockDispatcher::Create(fd, file_size, read_only, dispatcher);
    case DataPlane::FIFO:
      return FifoBlockDispatcher::Create(fd, file_size, read_only, phys_mem,
                                         dispatcher);
    default:
      FXL_LOG(ERROR) << "Unsupported block dispatcher data plane";
      return ZX_ERR_INVALID_ARGS;
  }
}

}  // namespace machina
