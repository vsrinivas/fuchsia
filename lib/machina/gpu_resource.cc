// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/gpu_resource.h"

#include "garnet/lib/machina/gpu_scanout.h"

fbl::unique_ptr<GpuResource> GpuResource::Create(
    const virtio_gpu_resource_create_2d_t* request,
    VirtioGpu* gpu) {
  SkBitmap bitmap;
  bitmap.setInfo(SkImageInfo::MakeN32(request->width, request->height,
                                      kOpaque_SkAlphaType));
  bitmap.allocPixels();
  return fbl::make_unique<GpuResource>(gpu, request->resource_id,
                                       fbl::move(bitmap));
}

GpuResource::GpuResource(VirtioGpu* gpu, ResourceId id, SkBitmap bitmap)
    : gpu_(gpu), res_id_(id), bitmap_(fbl::move(bitmap)) {}

virtio_gpu_ctrl_type GpuResource::AttachBacking(
    const virtio_gpu_mem_entry_t* mem_entries,
    uint32_t num_entries) {
  const size_t required_bytes = width() * height() * VirtioGpu::kBytesPerPixel;
  size_t backing_size = 0;
  for (int i = num_entries - 1; i >= 0; --i) {
    const virtio_gpu_mem_entry_t* entry = &mem_entries[i];
    backing_.push_front(
        fbl::make_unique<BackingPages>(entry->addr, entry->length));
    backing_size += entry->length;
  }
  if (backing_size < required_bytes) {
    fprintf(
        stderr,
        "virtio-gpu: attach backing command provided buffer is too small.\n");
    backing_.clear();
    return VIRTIO_GPU_RESP_ERR_UNSPEC;
  }
  return VIRTIO_GPU_RESP_OK_NODATA;
}

virtio_gpu_ctrl_type GpuResource::DetachBacking() {
  backing_.clear();
  return VIRTIO_GPU_RESP_OK_NODATA;
}

virtio_gpu_ctrl_type GpuResource::TransferToHost2D(
    const virtio_gpu_transfer_to_host_2d_t* request) {
  if (bitmap_.isNull()) {
    return VIRTIO_GPU_RESP_ERR_UNSPEC;
  }
  if (backing_.is_empty()) {
    return VIRTIO_GPU_RESP_ERR_UNSPEC;
  }

  // Optimize for copying a contiguous region.
  uint8_t* pixel_ref = reinterpret_cast<uint8_t*>(bitmap_.getPixels());
  uint32_t stride = bitmap_.width() * VirtioGpu::kBytesPerPixel;
  if (request->offset == 0 && request->r.x == 0 && request->r.y == 0 &&
      request->r.width == static_cast<uint32_t>(bitmap_.width())) {
    CopyBytes(0, pixel_ref, stride * bitmap_.height());
    return VIRTIO_GPU_RESP_OK_NODATA;
  }

  // line-by-line copy.
  uint32_t linesize = request->r.width * 4;
  for (uint32_t line = 0; line < request->r.height; ++line) {
    uint64_t src_offset = request->offset + stride * line;
    size_t size = ((request->r.y + line) * stride) +
                  (request->r.x * VirtioGpu::kBytesPerPixel);

    CopyBytes(src_offset, pixel_ref + size, linesize);
  }
  return VIRTIO_GPU_RESP_OK_NODATA;
}

virtio_gpu_ctrl_type GpuResource::Flush(
    const virtio_gpu_resource_flush_t* request) {
  GpuScanout* scanout = scanout_;
  if (scanout == nullptr)
    return VIRTIO_GPU_RESP_OK_NODATA;

  // TODO: Convert r to scanout coordinates.
  scanout->FlushRegion(request->r);
  return VIRTIO_GPU_RESP_OK_NODATA;
}

void GpuResource::CopyBytes(uint64_t offset, uint8_t* dest, size_t size) {
  size_t base = 0;
  for (const auto& entry : backing_) {
    if (size == 0)
      break;
    if (base + entry.length > offset) {
      size_t len = (entry.length + base) - offset;
      len = len > size ? size : len;

      zx_vaddr_t src_vaddr = gpu_->guest_physmem_addr() + entry.addr;
      src_vaddr = src_vaddr + offset - base;

      memcpy(dest, reinterpret_cast<void*>(src_vaddr), len);

      dest += len;
      offset += len;
      size -= len;
    }
    base += entry.length;
  }
}
