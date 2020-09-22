// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_GPU_SCANOUT_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_GPU_SCANOUT_H_

#include <fuchsia/virtualization/hardware/cpp/fidl.h>
#include <lib/zx/vmo.h>
#include <virtio/gpu.h>

#include "src/virtualization/bin/vmm/device/gpu.h"

class GpuResource;

// A scanout represents a display that GPU resources can be flushed to.
class GpuScanout {
 public:
  virtio_gpu_rect_t extents() { return extents_; }

  void SetConfigChangedHandler(fit::closure config_changed_handler);

  // Set a source-update handler for this scanout. On receiving a SetScanout
  // command from the guest (e.g. resulting from a manual mode change), the
  // handler will be called with the new source dimensions from the VirtioGpu
  // device thread.
  void SetUpdateSourceHandler(fit::function<void(uint32_t, uint32_t)> update_source_handler);

  // Set a flush handler for this scanout. On receiving a Flush command from
  // the guest, the handler will be called with the flushed subrect from the
  // VirtioGpu device thread. The contents of the target will not be
  // subsequently modified until this handler returns.
  void SetFlushHandler(fit::function<void(virtio_gpu_rect_t)> flush_handler);

  // Set the flush target location for this scanout. On receiving a flush
  // command, the scanout will copy data from the source resource into the
  // target. The target will be written using the native pixel format of the
  // guest driver.
  // TODO(fxbug.dev/12530): expose pixel format to scanout clients
  zx_status_t SetFlushTarget(zx::vmo vmo, uint64_t size, uint32_t width, uint32_t height,
                             uint32_t stride);

  // Called in response to VIRTIO_GPU_CMD_SET_SCANOUT. This command associates
  // a particular GpuResource and subrect with the scanout.
  void OnSetScanout(const GpuResource* source_resource, const virtio_gpu_rect_t& source_rect);

  // Called in response to VIRTIO_GPU_CMD_RESOURCE_FLUSH. This command notifies
  // the device that the resource's contents should be flushed to any attached
  // scanouts whose source rect overlaps the flushed rect.
  void OnResourceFlush(const GpuResource* resource, const virtio_gpu_rect_t& rect);

  // Called in response to VIRTIO_GPU_CMD_UPDATE_CURSOR. This command
  // associates a particular cursor GpuResource metadata with the scanout.
  void OnUpdateCursor(const GpuResource* cursor_resource, uint32_t hot_x, uint32_t hot_y);

  // Called in response to VIRTIO_GPU_CMD_MOVE_CURSOR. This command notifies
  // the device that the cursor resource position should be updated. Also
  // called in response to VIRTIO_GPU_CMD_UPDATE_CURSOR as position updates are
  // included in that message.
  void OnMoveCursor(uint32_t x, uint32_t y);

 private:
  fit::closure config_changed_handler_;
  fit::function<void(uint32_t, uint32_t)> update_source_handler_;
  fit::function<void(virtio_gpu_rect_t)> flush_handler_;

  uint64_t target_size_;
  uint32_t target_width_;
  uint32_t target_height_;
  uint32_t target_stride_;
  zx::vmo target_vmo_;
  uintptr_t target_vmo_addr_;

  // Scanout parameters.
  virtio_gpu_rect_t extents_{0, 0, kGpuStartupWidth, kGpuStartupHeight};
  const GpuResource* source_resource_ = nullptr;
  virtio_gpu_rect_t source_rect_;
  const GpuResource* cursor_resource_ = nullptr;
  uint32_t cursor_x_;
  uint32_t cursor_y_;
  uint32_t cursor_hot_x_;
  uint32_t cursor_hot_y_;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_GPU_SCANOUT_H_
