// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/controller/virtio_block.h"

#include <lib/sys/cpp/service_directory.h>

#include "src/virtualization/bin/vmm/device/block.h"

static constexpr char kVirtioBlockUrl[] =
    "fuchsia-pkg://fuchsia.com/virtio_block#meta/virtio_block.cmx";

static bool read_only(fuchsia::virtualization::BlockMode mode) {
  return mode == fuchsia::virtualization::BlockMode::READ_ONLY;
}

VirtioBlock::VirtioBlock(fuchsia::virtualization::BlockMode mode, const PhysMem& phys_mem)
    : VirtioComponentDevice(
          phys_mem,
          // From Virtio 1.0, Section 5.2.5.2: Devices SHOULD always offer
          // VIRTIO_BLK_F_FLUSH.
          //
          // VIRTIO_BLK_F_BLK_SIZE is required by Zircon guests.
          VIRTIO_BLK_F_FLUSH | VIRTIO_BLK_F_BLK_SIZE | (read_only(mode) ? VIRTIO_BLK_F_RO : 0),
          fit::bind_member(this, &VirtioBlock::ConfigureQueue),
          fit::bind_member(this, &VirtioBlock::Ready)),
      mode_(mode) {}

zx_status_t VirtioBlock::Start(const zx::guest& guest, const std::string& id,
                               fuchsia::virtualization::BlockFormat format,
                               fuchsia::io::FilePtr file, fuchsia::sys::Launcher* launcher,
                               async_dispatcher_t* dispatcher) {
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
  uint64_t size;
  status = block_->Start(std::move(start_info), id, mode_, format, std::move(file), &size);
  if (status != ZX_OK) {
    return status;
  } else if (size % kBlockSectorSize != 0) {
    FXL_LOG(ERROR) << "Virtio block device must be aligned to block sector size: " << id
                   << " has size " << size << ".";
    return ZX_ERR_INVALID_ARGS;
  }

  std::lock_guard<std::mutex> lock(device_config_.mutex);
  config_.capacity = size / kBlockSectorSize;
  config_.blk_size = kBlockSectorSize;
  return ZX_OK;
}

zx_status_t VirtioBlock::ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc,
                                        zx_gpaddr_t avail, zx_gpaddr_t used) {
  return block_->ConfigureQueue(queue, size, desc, avail, used);
}

zx_status_t VirtioBlock::Ready(uint32_t negotiated_features) {
  return block_->Ready(negotiated_features);
}
