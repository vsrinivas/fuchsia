// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/controller/virtio_block.h"

#include <lib/sys/cpp/service_directory.h>

#include "src/virtualization/bin/vmm/device/block.h"

static constexpr char kVirtioBlockUrl[] =
    "fuchsia-pkg://fuchsia.com/virtio_block#meta/virtio_block.cmx";

static uint32_t read_only(fuchsia::virtualization::BlockMode mode) {
  return mode == fuchsia::virtualization::BlockMode::READ_ONLY ? VIRTIO_BLK_F_RO : 0;
}

static uint32_t discardable(fuchsia::virtualization::BlockFormat format) {
  // TODO(fxbug.dev/90622): Enable discard support if BLOCK is the format used.
  return 0;
}

VirtioBlock::VirtioBlock(const PhysMem& phys_mem, fuchsia::virtualization::BlockMode mode,
                         fuchsia::virtualization::BlockFormat format)
    : VirtioComponentDevice(
          "Virtio Block", phys_mem,
          // From Virtio 1.0, Section 5.2.5.2: Devices SHOULD always offer
          // VIRTIO_BLK_F_FLUSH.
          //
          // VIRTIO_BLK_F_BLK_SIZE is required by Zircon guests.
          VIRTIO_BLK_F_FLUSH | VIRTIO_BLK_F_BLK_SIZE | read_only(mode) | discardable(format),
          fit::bind_member(this, &VirtioBlock::ConfigureQueue),
          fit::bind_member(this, &VirtioBlock::Ready)),
      mode_(mode),
      format_(format) {}

zx_status_t VirtioBlock::Start(const zx::guest& guest, const std::string& id, zx::channel client,
                               fuchsia::sys::Launcher* launcher, async_dispatcher_t* dispatcher) {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kVirtioBlockUrl;
  auto services = sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);
  launcher->CreateComponent(std::move(launch_info), controller_.NewRequest());
  services->Connect(block_.NewRequest());

  fuchsia::virtualization::hardware::StartInfo start_info;
  zx_status_t status = PrepStart(guest, dispatcher, &start_info);
  if (status != ZX_OK) {
    return status;
  }
  uint64_t capacity;
  uint32_t block_size;
  status = block_->Start(std::move(start_info), id, mode_, format_, std::move(client), &capacity,
                         &block_size);
  if (status != ZX_OK) {
    return status;
  } else if (capacity % block_size != 0) {
    FX_LOGS(ERROR) << "Virtio block device capacity must be aligned to block size: " << id
                   << " has capacity " << capacity << " and block size " << block_size;
    return ZX_ERR_INVALID_ARGS;
  }

  std::lock_guard<std::mutex> lock(device_config_.mutex);
  config_.capacity = capacity / block_size;
  config_.blk_size = block_size;
  return ZX_OK;
}

zx_status_t VirtioBlock::ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc,
                                        zx_gpaddr_t avail, zx_gpaddr_t used) {
  return block_->ConfigureQueue(queue, size, desc, avail, used);
}

zx_status_t VirtioBlock::Ready(uint32_t negotiated_features) {
  return block_->Ready(negotiated_features);
}
