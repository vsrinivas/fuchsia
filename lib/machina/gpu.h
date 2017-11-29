// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_GPU_H_
#define GARNET_LIB_MACHINA_GPU_H_

#include <fbl/intrusive_hash_table.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <virtio/gpu.h>
#include <zircon/compiler.h>
#include <zircon/device/display.h>
#include <zircon/types.h>

#include "garnet/lib/machina/virtio.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkSurface.h"

#define VIRTIO_GPU_Q_CONTROLQ 0
#define VIRTIO_GPU_Q_CURSORQ 1
#define VIRTIO_GPU_Q_COUNT 2

class GpuResource;
class VirtioGpu;

using ResourceId = uint32_t;
using ScanoutId = uint32_t;

// A scanout represents a display that GPU resources can be rendered to.
class GpuScanout {
 public:
  GpuScanout(sk_sp<SkSurface>&& surface) : surface_(surface) {}

  virtual ~GpuScanout() = default;

  uint32_t width() const { return surface_->width(); }
  uint32_t height() const { return surface_->height(); }

  virtual void FlushRegion(const virtio_gpu_rect_t& rect);

  zx_status_t SetResource(GpuResource* res,
                          const virtio_gpu_set_scanout_t* request);

 private:
  sk_sp<SkSurface> surface_;

  // Scanout parameters.
  GpuResource* resource_;
  SkRect rect_;
};

// A resource corresponds to a single display buffer.
class GpuResource
    : public fbl::SinglyLinkedListable<fbl::unique_ptr<GpuResource>> {
 public:
  static fbl::unique_ptr<GpuResource> Create(
      const virtio_gpu_resource_create_2d_t* request,
      VirtioGpu* gpu);

  // The driver will provide a scatter-gather list of memory pages to back
  // the framebuffer in guest physical memory.
  struct BackingPages
      : public fbl::SinglyLinkedListable<fbl::unique_ptr<BackingPages>> {
    uint64_t addr;
    uint32_t length;

    BackingPages(uint64_t addr_, uint32_t length_)
        : addr(addr_), length(length_) {}
  };

  // Fix the number of hash table buckets to 1 because linux and zircon
  // virtcons only use a single resource.
  static constexpr size_t kNumHashTableBuckets = 1;
  using HashTable =
      fbl::HashTable<ResourceId,
                     fbl::unique_ptr<GpuResource>,
                     fbl::SinglyLinkedList<fbl::unique_ptr<GpuResource>>,
                     size_t,
                     kNumHashTableBuckets>;

  GpuResource(VirtioGpu*, ResourceId, SkBitmap);

  uint32_t width() const { return bitmap_.width(); }

  uint32_t height() const { return bitmap_.height(); }

  const SkBitmap& bitmap() const { return bitmap_; }

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

  SkBitmap bitmap_;
  GpuScanout* scanout_;
};

// Virtio 2D GPU device.
class VirtioGpu : public VirtioDevice {
 public:
  static constexpr uint8_t kBytesPerPixel = 4;

  VirtioGpu(uintptr_t guest_physmem_addr, size_t guest_physmem_size);
  ~VirtioGpu() override = default;

  virtio_queue_t& control_queue() { return queues_[VIRTIO_GPU_Q_CONTROLQ]; }
  virtio_queue_t& cursor_queue() { return queues_[VIRTIO_GPU_Q_CURSORQ]; }

  // Opens the framebuffer device located at |path| and starts processing
  // any descriptors that become available in the queues.
  zx_status_t Init(const char* path);

  // Adds a scanout to the GPU.
  //
  // Currently only a single scanout is supported. ZX_ERR_ALREADY_EXISTS will
  // be returned if this method is called multiple times.
  zx_status_t AddScanout(fbl::unique_ptr<GpuScanout> scanout);

  zx_status_t HandleGpuCommand(virtio_queue_t* queue,
                               uint16_t head,
                               uint32_t* used);

 protected:
  static zx_status_t QueueHandler(virtio_queue_t* queue,
                                  uint16_t head,
                                  uint32_t* used,
                                  void* ctx);

  // VIRTIO_GPU_CMD_GET_DISPLAY_INFO
  void GetDisplayInfo(const virtio_gpu_ctrl_hdr_t* request,
                      virtio_gpu_resp_display_info_t* response);

  // VIRTIO_GPU_CMD_RESOURCE_CREATE_2D
  void ResourceCreate2D(const virtio_gpu_resource_create_2d_t* request,
                        virtio_gpu_ctrl_hdr_t* response);

  // VIRTIO_GPU_CMD_RESOURCE_UNREF
  void ResourceUnref(const virtio_gpu_resource_unref_t* request,
                     virtio_gpu_ctrl_hdr_t* response);

  // VIRTIO_GPU_CMD_SET_SCANOUT
  void SetScanout(const virtio_gpu_set_scanout_t* request,
                  virtio_gpu_ctrl_hdr_t* response);

  // VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING
  void ResourceAttachBacking(
      const virtio_gpu_resource_attach_backing_t* request,
      const virtio_gpu_mem_entry_t* mem_entries,
      virtio_gpu_ctrl_hdr_t* response);

  // VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING
  void ResourceDetachBacking(
      const virtio_gpu_resource_detach_backing_t* request,
      virtio_gpu_ctrl_hdr_t* response);

  // VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D
  void TransferToHost2D(const virtio_gpu_transfer_to_host_2d_t* request,
                        virtio_gpu_ctrl_hdr_t* response);

  // VIRTIO_GPU_CMD_RESOURCE_FLUSH
  void ResourceFlush(const virtio_gpu_resource_flush_t* request,
                     virtio_gpu_ctrl_hdr_t* response);

 private:
  fbl::unique_ptr<GpuScanout> scanout_;
  GpuResource::HashTable resources_;
  virtio_queue_t queues_[VIRTIO_GPU_Q_COUNT];
  virtio_gpu_config_t config_ = {};
};

#endif  // GARNET_LIB_MACHINA_GPU_H_
