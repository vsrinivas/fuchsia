// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_BLOCK_H_
#define GARNET_LIB_MACHINA_VIRTIO_BLOCK_H_

#include <mutex>

#include <virtio/block.h>
#include <virtio/virtio_ids.h>

#include "garnet/lib/machina/block_dispatcher.h"
#include "garnet/lib/machina/virtio_device.h"

namespace machina {

// Stores the state of a block device.
class VirtioBlock
    : public VirtioDevice<VIRTIO_ID_BLOCK, 1, virtio_blk_config_t> {
 public:
  static constexpr size_t kSectorSize = 512;

  VirtioBlock(const PhysMem& phys_mem,
              std::unique_ptr<BlockDispatcher> dispatcher);

  // Starts a thread to monitor the queue for incoming block requests.
  zx_status_t Start();

  zx_status_t HandleBlockRequest(VirtioQueue* queue, uint16_t head,
                                 uint32_t* used);

  bool is_read_only() { return pci_.has_device_features(VIRTIO_BLK_F_RO); }

  // The queue used for handling block requests.
  VirtioQueue* request_queue() { return queue(0); }

 private:
  std::unique_ptr<BlockDispatcher> dispatcher_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_BLOCK_H_
