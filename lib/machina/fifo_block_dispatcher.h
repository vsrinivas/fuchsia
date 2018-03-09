// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_FIFO_BLOCK_DISPATCHER_H_
#define GARNET_LIB_MACHINA_FIFO_BLOCK_DISPATCHER_H_

#include <block-client/client.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>

#include "garnet/lib/machina/block_dispatcher.h"
#include "garnet/lib/machina/phys_mem.h"

namespace machina {

class FifoBlockDispatcher : public BlockDispatcher {
 public:
  static zx_status_t Create(int fd,
                            size_t size,
                            bool read_only,
                            const PhysMem& phys_mem,
                            fbl::unique_ptr<BlockDispatcher>* out);

  FifoBlockDispatcher(size_t size,
                      bool read_only,
                      int fd,
                      txnid_t txnid,
                      vmoid_t vmoid,
                      fifo_client_t* fifo_client,
                      size_t guest_vmo_addr);

  ~FifoBlockDispatcher();

  zx_status_t Flush() override { return ZX_OK; }
  zx_status_t Read(off_t disk_offset, void* buf, size_t size) override;
  zx_status_t Write(off_t disk_offset, const void* buf, size_t size) override;
  zx_status_t Submit() override;

 private:
  zx_status_t EnqueueBlockRequestLocked(uint16_t opcode,
                                        off_t disk_offset,
                                        const void* buf,
                                        size_t size) __TA_REQUIRES(fifo_mutex_);

  zx_status_t SubmitTransactionsLocked() __TA_REQUIRES(fifo_mutex_);

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

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_FIFO_BLOCK_DISPATCHER_H_
