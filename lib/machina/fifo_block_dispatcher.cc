// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/fifo_block_dispatcher.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>

namespace machina {

zx_status_t FifoBlockDispatcher::Create(int fd,
                                        size_t size,
                                        bool read_only,
                                        const PhysMem& phys_mem,
                                        fbl::unique_ptr<BlockDispatcher>* out) {
  zx_handle_t fifo;
  ssize_t result = ioctl_block_get_fifos(fd, &fifo);
  if (result != sizeof(fifo))
    return ZX_ERR_IO;
  auto close_fifo = fbl::MakeAutoCall([fifo] { zx_handle_close(fifo); });

  txnid_t txnid = TXNID_INVALID;
  result = ioctl_block_alloc_txn(fd, &txnid);
  if (result != sizeof(txnid_))
    return ZX_ERR_IO;
  auto free_txn =
      fbl::MakeAutoCall([fd, txnid] { ioctl_block_free_txn(fd, &txnid); });

  zx_handle_t vmo_dup;
  zx_status_t status =
      zx_handle_duplicate(phys_mem.vmo().get(), ZX_RIGHT_SAME_RIGHTS, &vmo_dup);
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
      [fifo_client] { block_fifo_release_client(fifo_client); });

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

FifoBlockDispatcher::FifoBlockDispatcher(size_t size,
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

FifoBlockDispatcher::~FifoBlockDispatcher() {
  if (txnid_ != TXNID_INVALID) {
    ioctl_block_free_txn(fd_, &txnid_);
  }
  if (fifo_client_ != nullptr) {
    block_fifo_release_client(fifo_client_);
  }
}

zx_status_t FifoBlockDispatcher::Read(off_t disk_offset,
                                      void* buf,
                                      size_t size) {
  fbl::AutoLock lock(&fifo_mutex_);
  return EnqueueBlockRequestLocked(BLOCKIO_READ, disk_offset, buf, size);
}

zx_status_t FifoBlockDispatcher::Write(off_t disk_offset,
                                       const void* buf,
                                       size_t size) {
  fbl::AutoLock lock(&fifo_mutex_);
  return EnqueueBlockRequestLocked(BLOCKIO_WRITE, disk_offset, buf, size);
}

zx_status_t FifoBlockDispatcher::Submit() {
  fbl::AutoLock lock(&fifo_mutex_);
  return SubmitTransactionsLocked();
}

zx_status_t FifoBlockDispatcher::EnqueueBlockRequestLocked(uint16_t opcode,
                                                           off_t disk_offset,
                                                           const void* buf,
                                                           size_t size) {
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

zx_status_t FifoBlockDispatcher::SubmitTransactionsLocked() {
  zx_status_t status = block_fifo_txn(fifo_client_, requests_, request_index_);
  request_index_ = 0;
  return status;
}

}  // namespace machina
