// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/device/gpu_scanout.h"

#include <lib/zx/vmar.h>

#include "src/virtualization/bin/vmm/device/gpu_resource.h"

void GpuScanout::SetConfigChangedHandler(fit::closure config_changed_handler) {
  config_changed_handler_ = std::move(config_changed_handler);
}

void GpuScanout::SetUpdateSourceHandler(
    fit::function<void(uint32_t, uint32_t)> update_source_handler) {
  update_source_handler_ = std::move(update_source_handler);
}

void GpuScanout::SetFlushHandler(fit::function<void(virtio_gpu_rect_t)> flush_handler) {
  flush_handler_ = std::move(flush_handler);
}

zx_status_t GpuScanout::SetFlushTarget(zx::vmo vmo, uint64_t size, uint32_t width, uint32_t height,
                                       uint32_t stride) {
  // Bind the target and map its memory into our process.
  target_vmo_ = std::move(vmo);
  target_size_ = size;
  target_width_ = width;
  target_height_ = height;
  target_stride_ = stride;
  zx_status_t status = zx::vmar::root_self()->map(
      ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, target_vmo_, 0, target_size_, &target_vmo_addr_);
  if (status != ZX_OK) {
    return status;
  }

  // Notify the client of the current guest source dimensions, in case this is
  // the first time it has attached.
  if (update_source_handler_) {
    update_source_handler_(extents_.width, extents_.height);
  }

  // Update the scanout extents to match the target.
  extents_.width = width;
  extents_.height = height;
  config_changed_handler_();

  // Force a flush of the entire source region to populate the new target.
  if (source_resource_) {
    OnResourceFlush(source_resource_, source_rect_);
  }
  return ZX_OK;
}

void GpuScanout::OnSetScanout(const GpuResource* source_resource,
                              const virtio_gpu_rect_t& source_rect) {
  source_resource_ = source_resource;
  source_rect_ = source_rect;
  if (update_source_handler_) {
    update_source_handler_(source_rect.width, source_rect.height);
  }
}

static bool overlaps(virtio_gpu_rect_t a, virtio_gpu_rect_t b) {
  if (a.x > (b.x + b.width) || b.x > (a.x + a.width)) {
    return false;
  } else if (a.y > (b.y + b.height) || b.y > (a.y + a.height)) {
    return false;
  } else {
    return true;
  }
}

static virtio_gpu_rect_t clip(virtio_gpu_rect_t rect, virtio_gpu_rect_t clip) {
  if (rect.x < clip.x) {
    rect.width -= clip.x - rect.x;
    rect.x = clip.x;
  }
  if (rect.y < clip.y) {
    rect.height -= clip.y - rect.y;
    rect.y = clip.y;
  }
  if (rect.x + rect.width > clip.x + clip.width) {
    rect.width = clip.x + clip.width - rect.x;
  }
  if (rect.y + rect.height > clip.y + clip.height) {
    rect.height = clip.y + clip.height - rect.y;
  }
  return rect;
}

void GpuScanout::OnResourceFlush(const GpuResource* resource, const virtio_gpu_rect_t& rect) {
  if (resource != source_resource_ || !overlaps(rect, source_rect_)) {
    return;
  }
  virtio_gpu_rect_t flush_rect = clip(rect, extents_);
  if (target_vmo_) {
    // Copy the flushed region to the target.
    uint32_t row_begin = flush_rect.y;
    uint32_t row_end = std::min(flush_rect.y + flush_rect.height, target_height_);
    uint32_t row_bytes =
        std::min(flush_rect.width, target_width_ - flush_rect.x) * resource->pixel_size();
    for (uint32_t row = row_begin; row < row_end; ++row) {
      uint8_t* dest = reinterpret_cast<uint8_t*>(target_vmo_addr_) + target_stride_ * row +
                      flush_rect.x * resource->pixel_size();
      const uint8_t* src = source_resource_->data() + source_resource_->stride() * row +
                           flush_rect.x * resource->pixel_size();
      memcpy(dest, src, row_bytes);
    }
  }
  if (flush_handler_) {
    flush_handler_(flush_rect);
  }
}

void GpuScanout::OnUpdateCursor(const GpuResource* cursor_resource, uint32_t hot_x,
                                uint32_t hot_y) {
  cursor_resource_ = cursor_resource;
  cursor_hot_x_ = hot_x;
  cursor_hot_y_ = hot_y;
}

void GpuScanout::OnMoveCursor(uint32_t x, uint32_t y) {
  cursor_x_ = x;
  cursor_y_ = y;
}
