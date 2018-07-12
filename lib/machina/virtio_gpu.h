// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_GPU_H_
#define GARNET_LIB_MACHINA_VIRTIO_GPU_H_

#include <fbl/intrusive_hash_table.h>
#include <fbl/unique_ptr.h>
#include <lib/async/cpp/wait.h>
#include <virtio/gpu.h>
#include <virtio/virtio_ids.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "garnet/lib/machina/virtio_device.h"

#define VIRTIO_GPU_Q_CONTROLQ 0
#define VIRTIO_GPU_Q_CURSORQ 1
#define VIRTIO_GPU_Q_COUNT 2

namespace machina {

class GpuResource;
class GpuScanout;
class VirtioGpu;

using ResourceId = uint32_t;
using ScanoutId = uint32_t;

// Virtio 2D GPU device.
class VirtioGpu : public VirtioDeviceBase<VIRTIO_ID_GPU, VIRTIO_GPU_Q_COUNT,
                                          virtio_gpu_config_t> {
 public:
  VirtioGpu(const PhysMem& phys_mem, async_dispatcher_t* dispatcher);
  ~VirtioGpu() override;

  VirtioQueue* control_queue() { return queue(VIRTIO_GPU_Q_CONTROLQ); }
  VirtioQueue* cursor_queue() { return queue(VIRTIO_GPU_Q_CURSORQ); }

  // Begins processing any descriptors that become available in the queues.
  zx_status_t Init();

  // Adds a scanout to the GPU.
  //
  // Currently only a single scanout is supported. ZX_ERR_ALREADY_EXISTS will
  // be returned if this method is called multiple times.
  zx_status_t AddScanout(GpuScanout* scanout);

  zx_status_t HandleGpuCommand(VirtioQueue* queue, uint16_t head,
                               uint32_t* used);

 protected:
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

  // VIRTIO_GPU_CMD_UPDATE_CURSOR
  // VIRTIO_GPU_CMD_MOVE_CURSOR
  void MoveOrUpdateCursor(const virtio_gpu_update_cursor_t* request);

 private:
  GpuScanout* scanout_ = nullptr;

  // Fix the number of hash table buckets to 1 because linux and zircon
  // virtcons only use a single resource.
  static constexpr size_t kNumHashTableBuckets = 1;
  using ResourceTable =
      fbl::HashTable<ResourceId, fbl::unique_ptr<GpuResource>,
                     fbl::SinglyLinkedList<fbl::unique_ptr<GpuResource>>,
                     size_t, kNumHashTableBuckets>;

  ResourceTable resources_;
  async_dispatcher_t* dispatcher_;
  async::Wait control_queue_wait_;
  async::Wait cursor_queue_wait_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_GPU_H_
