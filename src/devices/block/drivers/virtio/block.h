// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_STORAGE_BLOCK_DRIVERS_VIRTIO_BLOCK_H_
#define SRC_STORAGE_BLOCK_DRIVERS_VIRTIO_BLOCK_H_

#include <lib/sync/completion.h>
#include <stdlib.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>

#include <atomic>
#include <memory>

#include <ddk/protocol/block.h>
#include <ddktl/device.h>
#include <ddktl/protocol/block.h>
#include <virtio/block.h>

#include "src/devices/bus/lib/virtio/backends/backend.h"
#include "src/devices/bus/lib/virtio/device.h"
#include "src/devices/bus/lib/virtio/ring.h"

namespace virtio {

struct block_txn_t {
  block_op_t op;
  block_impl_queue_callback completion_cb;
  void* cookie;
  struct vring_desc* desc;
  size_t index;
  list_node_t node;
  zx_handle_t pmt;
};

class Ring;
class BlockDevice;
using DeviceType =
    ddk::Device<BlockDevice, ddk::GetProtocolable, ddk::GetSizable, ddk::UnbindableNew>;
class BlockDevice : public Device,
                    // Mixins for protocol device:
                    public DeviceType,
                    // Mixin for Block banjo protocol:
                    public ddk::BlockImplProtocol<BlockDevice, ddk::base_protocol> {
 public:
  BlockDevice(zx_device_t* device, zx::bti bti, std::unique_ptr<Backend> backend);

  virtual zx_status_t Init() override;

  // DDKTL device hooks:
  void DdkRelease();
  void DdkUnbindNew(ddk::UnbindTxn txn);
  zx_off_t DdkGetSize() const { return config_.capacity * config_.blk_size; }
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);

  virtual void IrqRingUpdate() override;
  virtual void IrqConfigChange() override;

  uint32_t GetBlockSize() const { return config_.blk_size; }
  uint64_t GetBlockCount() const { return config_.capacity; }
  const char* tag() const override { return "virtio-blk"; }

  // DDKTL Block protocol banjo functions:
  void BlockImplQuery(block_info_t* bi, size_t* bopsz);
  void BlockImplQueue(block_op_t* bop, block_impl_queue_callback completion_cb, void* cookie);

 private:
  void SignalWorker(block_txn_t* txn);
  void WorkerThread();
  void FlushPendingTxns();
  void CleanupPendingTxns();

  zx_status_t QueueTxn(block_txn_t* txn, uint32_t type, size_t bytes, zx_paddr_t* pages,
                       size_t pagecount, uint16_t* idx);

  void txn_complete(block_txn_t* txn, zx_status_t status);

  // The main virtio ring.
  Ring vring_ = {this};

  // Lock to be used around Ring::AllocDescChain and FreeDesc.
  // TODO: Move this into Ring class once it's certain that other users of the class are okay with
  // it.
  fbl::Mutex ring_lock_;

  static const uint16_t ring_size = 128;  // 128 matches legacy pci.

  // Saved block device configuration out of the pci config BAR.
  virtio_blk_config_t config_ = {};

  // A queue of block request/responses.
  static const size_t blk_req_count = 32;

  io_buffer_t blk_req_buf_;
  virtio_blk_req_t* blk_req_ = nullptr;

  zx_paddr_t blk_res_pa_ = 0;
  uint8_t* blk_res_ = nullptr;

  uint32_t blk_req_bitmap_ = 0;
  static_assert(blk_req_count <= sizeof(blk_req_bitmap_) * CHAR_BIT, "");

  size_t alloc_blk_req() {
    size_t i = 0;
    if (blk_req_bitmap_ != 0) {
      i = sizeof(blk_req_bitmap_) * CHAR_BIT - __builtin_clz(blk_req_bitmap_);
    }
    if (i < blk_req_count) {
      blk_req_bitmap_ |= (1 << i);
    }
    return i;
  }

  void free_blk_req(size_t i) { blk_req_bitmap_ &= ~(1 << i); }

  // Pending txns and completion signal.
  fbl::Mutex txn_lock_;
  list_node pending_txn_list_ = LIST_INITIAL_VALUE(pending_txn_list_);
  sync_completion_t txn_signal_;

  // Worker state.
  thrd_t worker_thread_;
  list_node worker_txn_list_ = LIST_INITIAL_VALUE(worker_txn_list_);
  sync_completion_t worker_signal_;
  std::atomic_bool worker_shutdown_ = false;
};

}  // namespace virtio

#endif  // SRC_STORAGE_BLOCK_DRIVERS_VIRTIO_BLOCK_H_
