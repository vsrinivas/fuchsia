// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_BLOCK_H_
#define SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_BLOCK_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/virtualization/hardware/cpp/fidl.h>

#include <virtio/block.h>
#include <virtio/virtio_ids.h>

#include "src/virtualization/bin/vmm/virtio_device.h"

static constexpr uint16_t kVirtioBlockNumQueues = 1;

class VirtioBlock
    : public VirtioComponentDevice<VIRTIO_ID_BLOCK, kVirtioBlockNumQueues, virtio_blk_config_t> {
 public:
  VirtioBlock(const PhysMem& phys_mem, fuchsia::virtualization::BlockMode mode);

  zx_status_t Start(const zx::guest& guest, const std::string& id,
                    fuchsia::virtualization::BlockFormat format, fuchsia::io::FilePtr file,
                    fuchsia::sys::Launcher* launcher, async_dispatcher_t* dispatcher);

 private:
  fuchsia::virtualization::BlockMode mode_;
  fuchsia::sys::ComponentControllerPtr controller_;
  // Use a sync pointer for consistency of virtual machine execution.
  fuchsia::virtualization::hardware::VirtioBlockSyncPtr block_;

  zx_status_t ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail,
                             zx_gpaddr_t used);
  zx_status_t Ready(uint32_t negotiated_features);
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_BLOCK_H_
