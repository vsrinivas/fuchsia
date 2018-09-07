// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_GPU_RESOURCE_H_
#define GARNET_LIB_MACHINA_GPU_RESOURCE_H_

#include <lib/fxl/macros.h>
#include <virtio/gpu.h>
#include <zircon/types.h>

#include <memory>
#include <vector>

#include "garnet/lib/machina/virtio_gpu.h"

namespace machina {

// A 2D GPU resource encapsulating guest and host memory.
class GpuResource {
 public:
  static virtio_gpu_ctrl_type Create(const PhysMem* phys_mem, uint32_t format,
                                     uint32_t width, uint32_t height,
                                     std::unique_ptr<GpuResource>* out);
  GpuResource(GpuResource&&) = default;

  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  uint32_t stride() const { return width() * kPixelSizeInBytes; }
  uint32_t pixel_size() const { return kPixelSizeInBytes; }
  const uint8_t* data() const { return host_backing_.get(); }

  // Called in response to VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING. This command
  // associates a set of guest memory pages with the resource.
  virtio_gpu_ctrl_type AttachBacking(const virtio_gpu_mem_entry_t* mem_entries,
                                     uint32_t num_entries);

  // Called in response to VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING. This command
  // clears guest memory associations with the resource.
  virtio_gpu_ctrl_type DetachBacking();

  // Called in response to VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D. This command
  // notifies the device that it should walk the set of guest backing pages and
  // copy the requested content region to host memory.
  virtio_gpu_ctrl_type TransferToHost2D(const virtio_gpu_rect_t& rect,
                                        uint64_t offset);

 private:
  GpuResource() = default;
  FXL_DISALLOW_COPY_AND_ASSIGN(GpuResource);

  void CopyBytes(uint64_t offset, uint8_t* dest, size_t size);

  static constexpr uint32_t kPixelSizeInBytes = 4;

  const PhysMem* phys_mem_;
  uint32_t format_;
  uint32_t width_;
  uint32_t height_;
  struct BackingPage {
    uint64_t addr;
    uint32_t length;
  };
  std::vector<BackingPage> guest_backing_;
  std::unique_ptr<uint8_t[]> host_backing_;
  size_t host_backing_size_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_GPU_RESOURCE_H_
