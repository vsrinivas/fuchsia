// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/controller/virtio_block.h"

#include <fuchsia/virtualization/hardware/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>
#include <zircon/errors.h>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/virtualization/bin/vmm/device/block.h"

namespace {

constexpr auto kVirtioBlockCollectionName = "virtio_block_devices";
constexpr auto component_url = "fuchsia-pkg://fuchsia.com/virtio_block#meta/virtio_block.cm";

uint32_t read_only(fuchsia::virtualization::BlockMode mode) {
  return mode == fuchsia::virtualization::BlockMode::READ_ONLY ? VIRTIO_BLK_F_RO : 0;
}

uint32_t discardable(fuchsia::virtualization::BlockFormat format) {
  // TODO(fxbug.dev/90622): Enable discard support if BLOCK is the format used.
  return 0;
}

}  // namespace

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
                               ::sys::ComponentContext* context, async_dispatcher_t* dispatcher,
                               size_t component_name_suffix) {
  std::string component_name = fxl::StringPrintf("virtio_block_%zu", component_name_suffix);
  zx_status_t status = CreateDynamicComponent(
      context, kVirtioBlockCollectionName, component_name.c_str(), component_url,
      [block = block_.NewRequest()](std::shared_ptr<sys::ServiceDirectory> services) mutable {
        return services->Connect(std::move(block));
      });
  if (status != ZX_OK) {
    return status;
  }

  fuchsia::virtualization::hardware::StartInfo start_info;
  status = PrepStart(guest, dispatcher, &start_info);
  if (status != ZX_OK) {
    return status;
  }
  uint64_t capacity;
  uint32_t block_size;
  status = block_->Start(std::move(start_info), id, mode_, format_, std::move(client), &capacity,
                         &block_size);
  if (status != ZX_OK) {
    return status;
  }

  // Capacity is expressed in terms fixed size sectors (512 bytes) and not the devices preferred
  // block size.
  //
  // Virtio 1.0, Section 5.2.4: The capacity of the device (expressed in 512-byte sectors) is
  // always present.
  //
  // Virtio 1.0, Section 2.5.2: If the VIRTIO_BLK_F_BLK_SIZE feature is negotiated, blk_size can be
  // read to determine the optimal sector size for the driver to use. This does not affect the units
  // used in the protocol (always 512 bytes), but awareness of the correct value can affect
  // performance.
  if (capacity % kBlockSectorSize != 0) {
    FX_LOGS(ERROR) << "Virtio block device capacity must be aligned to 512 byte sectors: " << id
                   << " has capacity " << capacity << " and block size " << block_size;
    return ZX_ERR_INVALID_ARGS;
  }

  std::lock_guard<std::mutex> lock(device_config_.mutex);
  config_.capacity = capacity / kBlockSectorSize;
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
