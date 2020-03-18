// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/device/gpu_resource.h"

#include "src/lib/syslog/cpp/logger.h"

GpuResource::GpuResource(const PhysMem& phys_mem, uint32_t format, uint32_t width, uint32_t height)
    : phys_mem_(&phys_mem),
      format_(format),
      width_(width),
      height_(height),
      host_backing_size_(width * height * kPixelSizeInBytes),
      host_backing_(std::make_unique<uint8_t[]>(host_backing_size_)) {}

void GpuResource::AttachBacking(const virtio_gpu_mem_entry_t* mem_entries, uint32_t num_entries) {
  // NOTE: it is valid for driver to leave regions of the image without backing,
  // so long as a transfer is never requested for them.
  guest_backing_.resize(num_entries);
  for (uint32_t i = 0; i < num_entries; ++i) {
    guest_backing_[i] = {
        .addr = mem_entries[i].addr,
        .len = mem_entries[i].length,
    };
  }
}

void GpuResource::DetachBacking() { guest_backing_.clear(); }

virtio_gpu_ctrl_type GpuResource::TransferToHost2d(const virtio_gpu_rect_t& rect, uint64_t off) {
  if (rect.x + rect.width > width_ || rect.y + rect.height > height_ ||
      (rect.y * width_ + rect.x) * kPixelSizeInBytes != off) {
    FX_LOGS(WARNING) << "Driver requested transfer of invalid resource region";
    return VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER;
  }
  const size_t rect_row_bytes = rect.width * kPixelSizeInBytes;
  const size_t image_row_bytes = width_ * kPixelSizeInBytes;
  size_t transfer_bytes_remaining = rect_row_bytes * rect.height;
  size_t rect_row_bytes_remaining = rect_row_bytes;
  uint64_t entry_off = 0;
  for (const auto& entry : guest_backing_) {
    if (transfer_bytes_remaining == 0) {
      break;
    }
    while (entry_off + entry.len > off && transfer_bytes_remaining > 0) {
      // Current entry covers requested content.
      size_t copy_size = std::min((entry_off + entry.len) - off, transfer_bytes_remaining);
      uint64_t off_next = off + copy_size;

      // If the copy rect width does not match the resource width, additional
      // logic is required to skip data between rows.
      if (rect.width != width_) {
        if (rect_row_bytes_remaining <= copy_size) {
          // Clamp the copy size to the rect row size.
          copy_size = rect_row_bytes_remaining;
          // Set the next offset to the start of the next image row.
          off_next = (off + image_row_bytes + rect_row_bytes_remaining) - rect_row_bytes;
          // Reset remaining bytes in the rect row.
          rect_row_bytes_remaining = rect_row_bytes;
        } else {
          rect_row_bytes_remaining -= copy_size;
        }
      }

      zx_vaddr_t src_vaddr = entry.addr + off - entry_off;
      memcpy(&host_backing_[off], phys_mem_->as<void>(src_vaddr, copy_size), copy_size);
      transfer_bytes_remaining -= copy_size;
      off = off_next;
    }
    entry_off += entry.len;
  }
  if (transfer_bytes_remaining > 0) {
    FX_LOGS(WARNING) << "Transfer requested from unbacked pages";
    memset(&host_backing_[off], 0, transfer_bytes_remaining);
    return VIRTIO_GPU_RESP_ERR_UNSPEC;
  }
  return VIRTIO_GPU_RESP_OK_NODATA;
}
