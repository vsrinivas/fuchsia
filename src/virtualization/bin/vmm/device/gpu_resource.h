// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_GPU_RESOURCE_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_GPU_RESOURCE_H_

#include <virtio/gpu.h>

#include <memory>
#include <vector>

#include "src/virtualization/bin/vmm/device/phys_mem.h"

// A 2D GPU resource encapsulating guest and host memory.
class GpuResource {
 public:
  GpuResource(const PhysMem& phys_mem, uint32_t format, uint32_t width,
              uint32_t height);
  GpuResource(GpuResource&&) = default;
  GpuResource& operator=(GpuResource&&) = default;

  GpuResource(const GpuResource&) = delete;
  GpuResource& operator=(const GpuResource&) = delete;

  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  uint32_t stride() const { return width() * kPixelSizeInBytes; }
  uint32_t pixel_size() const { return kPixelSizeInBytes; }
  const uint8_t* data() const { return host_backing_.get(); }

  // Called in response to VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING. This command
  // associates a set of guest memory pages with the resource.
  void AttachBacking(const virtio_gpu_mem_entry_t* mem_entries,
                     uint32_t num_entries);

  // Called in response to VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING. This command
  // clears guest memory associations with the resource.
  void DetachBacking();

  // Called in response to VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D. This command
  // notifies the device that it should walk the set of guest backing pages and
  // copy the requested content region to host memory.
  virtio_gpu_ctrl_type TransferToHost2d(const virtio_gpu_rect_t& rect,
                                        uint64_t off);

 private:
  static constexpr uint32_t kPixelSizeInBytes = 4;

  const PhysMem* phys_mem_;
  uint32_t format_;
  uint32_t width_;
  uint32_t height_;

  struct BackingPage {
    uint64_t addr;
    uint32_t len;
  };
  std::vector<BackingPage> guest_backing_;
  size_t host_backing_size_;
  std::unique_ptr<uint8_t[]> host_backing_;

  void CopyBytes(uint64_t off, uint8_t* dest, size_t size);
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_GPU_RESOURCE_H_
