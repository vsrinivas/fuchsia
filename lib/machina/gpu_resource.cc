// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/gpu_resource.h"
#include <lib/fxl/logging.h>

namespace machina {

virtio_gpu_ctrl_type GpuResource::Create(const PhysMem* phys_mem,
                                         uint32_t format, uint32_t width,
                                         uint32_t height,
                                         std::unique_ptr<GpuResource>* out) {
  *out = nullptr;
  // Note: std::make_unique unavailable due to private constructor.
  auto resource = std::unique_ptr<GpuResource>(new GpuResource());

  resource->phys_mem_ = phys_mem;
  resource->format_ = format;
  resource->width_ = width;
  resource->height_ = height;
  resource->host_backing_size_ = width * height * kPixelSizeInBytes;
  resource->host_backing_ =
      std::make_unique<uint8_t[]>(resource->host_backing_size_);
  if (!resource->host_backing_) {
    return VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY;
  }

  *out = std::move(resource);
  return VIRTIO_GPU_RESP_OK_NODATA;
}

virtio_gpu_ctrl_type GpuResource::AttachBacking(
    const virtio_gpu_mem_entry_t* mem_entries, uint32_t num_entries) {
  guest_backing_.resize(num_entries);
  for (uint32_t i = 0; i < num_entries; ++i) {
    guest_backing_[i].addr = mem_entries[i].addr;
    guest_backing_[i].length = mem_entries[i].length;
  }
  // Note that it is valid for driver to leave regions of the image without
  // backing, so long as a transfer is never requested for them.
  return VIRTIO_GPU_RESP_OK_NODATA;
}

virtio_gpu_ctrl_type GpuResource::DetachBacking() {
  guest_backing_.clear();
  return VIRTIO_GPU_RESP_OK_NODATA;
}

virtio_gpu_ctrl_type GpuResource::TransferToHost2D(
    const virtio_gpu_rect_t& rect, uint64_t offset) {
  if (rect.x + rect.width > width_ || rect.y + rect.height > height_ ||
      (rect.y * width_ + rect.x) * kPixelSizeInBytes != offset) {
    FXL_LOG(WARNING) << "Driver requested transfer of invalid resource region";
    return VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER;
  }
  const size_t rect_row_bytes = rect.width * kPixelSizeInBytes;
  const size_t image_row_bytes = width_ * kPixelSizeInBytes;
  size_t transfer_bytes_remaining = rect_row_bytes * rect.height;
  size_t rect_row_bytes_remaining = rect_row_bytes;
  uint64_t entry_offset = 0;
  for (const auto& entry : guest_backing_) {
    if (transfer_bytes_remaining == 0) {
      break;
    }
    while (entry_offset + entry.length > offset &&
           transfer_bytes_remaining > 0) {
      // Current entry covers requested content.
      size_t copy_size = std::min((entry_offset + entry.length) - offset,
                                  transfer_bytes_remaining);
      uint64_t offset_next = offset + copy_size;

      // If the copy rect width does not match the resource width, additional
      // logic is required to skip data between rows.
      if (rect.width != width_) {
        if (rect_row_bytes_remaining <= copy_size) {
          // Clamp the copy size to the rect row size.
          copy_size = rect_row_bytes_remaining;
          // Set the next offset to the start of the next image row.
          offset_next = (offset + image_row_bytes + rect_row_bytes_remaining) -
                        rect_row_bytes;
          // Reset remaining bytes in the rect row.
          rect_row_bytes_remaining = rect_row_bytes;
        } else {
          rect_row_bytes_remaining -= copy_size;
        }
      }

      zx_vaddr_t src_vaddr = entry.addr + offset - entry_offset;
      memcpy(&host_backing_[offset], phys_mem_->as<void>(src_vaddr, copy_size),
             copy_size);
      transfer_bytes_remaining -= copy_size;
      offset = offset_next;
    }
    entry_offset += entry.length;
  }
  if (transfer_bytes_remaining > 0) {
    FXL_LOG(WARNING) << "Transfer requested from unbacked pages";
    memset(&host_backing_[offset], 0, transfer_bytes_remaining);
    return VIRTIO_GPU_RESP_ERR_UNSPEC;
  }
  return VIRTIO_GPU_RESP_OK_NODATA;
}

}  // namespace machina
