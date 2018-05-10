// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_GPU_RESOURCE_H_
#define GARNET_LIB_MACHINA_GPU_RESOURCE_H_

#include <fbl/intrusive_single_list.h>
#include <fbl/unique_ptr.h>
#include <virtio/gpu.h>
#include <zircon/types.h>

#include "garnet/lib/machina/gpu_bitmap.h"
#include "garnet/lib/machina/virtio_gpu.h"

namespace machina {

class GpuScanout;

// A resource corresponds to a single display buffer.
class GpuResource
    : public fbl::SinglyLinkedListable<fbl::unique_ptr<GpuResource>> {
 public:
  static fbl::unique_ptr<GpuResource> Create(
      const virtio_gpu_resource_create_2d_t* request, VirtioGpu* gpu);

  // The driver will provide a scatter-gather list of memory pages to back
  // the framebuffer in guest physical memory.
  struct BackingPages
      : public fbl::SinglyLinkedListable<fbl::unique_ptr<BackingPages>> {
    uint64_t addr;
    uint32_t length;

    BackingPages(uint64_t addr_, uint32_t length_)
        : addr(addr_), length(length_) {}
  };

  GpuResource(VirtioGpu*, ResourceId, GpuBitmap);

  const GpuBitmap& bitmap() const { return bitmap_; }

  void AttachToScanout(GpuScanout* scanout) { scanout_ = scanout; }

  void DetachFromScanout() { scanout_ = nullptr; }

  virtio_gpu_ctrl_type SetScanout(GpuScanout* scanout);

  // Handle a VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING command for this
  // resource.
  virtio_gpu_ctrl_type AttachBacking(const virtio_gpu_mem_entry_t* mem_entries,
                                     uint32_t num_entries);

  // Handle a VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING command for this
  // resource.
  virtio_gpu_ctrl_type DetachBacking();

  // Handle a VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D command for this
  // resource.
  virtio_gpu_ctrl_type TransferToHost2D(
      const virtio_gpu_transfer_to_host_2d_t* request);

  // Handle a VIRTIO_GPU_CMD_RESOURCE_FLUSH command for this
  // resource.
  virtio_gpu_ctrl_type Flush(const virtio_gpu_resource_flush_t* request);

  // Handle a VIRTIO_GPU_CMD_SET_SCANOUT command for this
  // resource.
  virtio_gpu_ctrl_type Flush(GpuScanout* scanout);

  // Trait implementation for fbl::HashTable
  ResourceId GetKey() const { return res_id_; }
  static size_t GetHash(ResourceId key) { return key; }

 private:
  // Copies bytes from the linked list of backing pages in guest memory into
  // a host resource.
  void CopyBytes(uint64_t offset, uint8_t* dest, size_t size);

  VirtioGpu* gpu_;
  ResourceId res_id_;
  fbl::SinglyLinkedList<fbl::unique_ptr<BackingPages>> backing_;

  GpuBitmap bitmap_;
  GpuScanout* scanout_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_GPU_RESOURCE_H_
