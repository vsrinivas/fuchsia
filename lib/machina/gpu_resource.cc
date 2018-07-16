// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/gpu_resource.h"

#include <string.h>

#include "garnet/lib/machina/gpu_scanout.h"
#include "lib/fxl/logging.h"

static zx_pixel_format_t pixel_format(uint32_t virtio_format) {
  switch (virtio_format) {
    case VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM:
      return ZX_PIXEL_FORMAT_ARGB_8888;
    case VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM:
      return ZX_PIXEL_FORMAT_RGB_x888;
    default:
      return ZX_PIXEL_FORMAT_NONE;
  }
}

namespace machina {

fbl::unique_ptr<GpuResource> GpuResource::Create(
    const virtio_gpu_resource_create_2d_t* request, VirtioGpu* gpu) {
  zx_pixel_format_t format = pixel_format(request->format);
  if (format == ZX_PIXEL_FORMAT_NONE) {
    FXL_LOG(INFO) << "Unsupported GPU format " << request->format;
    return nullptr;
  }
  GpuBitmap bitmap(request->width, request->height, format);
  return fbl::make_unique<GpuResource>(gpu, request->resource_id,
                                       std::move(bitmap));
}

GpuResource::GpuResource(VirtioGpu* gpu, ResourceId id, GpuBitmap bitmap)
    : gpu_(gpu), res_id_(id), bitmap_(std::move(bitmap)) {}

virtio_gpu_ctrl_type GpuResource::AttachBacking(
    const virtio_gpu_mem_entry_t* mem_entries, uint32_t num_entries) {
  const size_t required_bytes =
      bitmap_.width() * bitmap_.height() * bitmap_.pixelsize();
  size_t backing_size = 0;
  for (int i = num_entries - 1; i >= 0; --i) {
    const virtio_gpu_mem_entry_t* entry = &mem_entries[i];
    backing_.push_front(
        fbl::make_unique<BackingPages>(entry->addr, entry->length));
    backing_size += entry->length;
  }
  if (backing_size < required_bytes) {
    FXL_LOG(ERROR) << "Attach backing command provided buffer is too small";
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
  if (bitmap_.buffer() == nullptr) {
    return VIRTIO_GPU_RESP_ERR_UNSPEC;
  }
  if (backing_.is_empty()) {
    return VIRTIO_GPU_RESP_ERR_UNSPEC;
  }

  // Optimize for copying a contiguous region.
  uint8_t* pixel_ref = bitmap_.buffer();
  uint32_t stride = bitmap_.width() * bitmap_.pixelsize();
  if (request->offset == 0 && request->r.x == 0 && request->r.y == 0 &&
      request->r.width == static_cast<uint32_t>(bitmap_.width())) {
    CopyBytes(0, pixel_ref, stride * bitmap_.height());
    return VIRTIO_GPU_RESP_OK_NODATA;
  }

  // line-by-line copy.
  uint32_t linesize = request->r.width * 4;
  for (uint32_t line = 0; line < request->r.height; ++line) {
    uint64_t src_offset = request->offset + stride * line;
    size_t size =
        ((request->r.y + line) * stride) + (request->r.x * bitmap_.pixelsize());
    CopyBytes(src_offset, pixel_ref + size, linesize);
  }
  return VIRTIO_GPU_RESP_OK_NODATA;
}

virtio_gpu_ctrl_type GpuResource::Flush(
    const virtio_gpu_resource_flush_t* request) {
  GpuScanout* scanout = scanout_;
  if (scanout == nullptr) {
    return VIRTIO_GPU_RESP_OK_NODATA;
  }

  scanout->DrawScanoutResource(request->r);
  return VIRTIO_GPU_RESP_OK_NODATA;
}

void GpuResource::CopyBytes(uint64_t offset, uint8_t* dest, size_t size) {
  size_t base = 0;
  for (const auto& entry : backing_) {
    if (size == 0) {
      break;
    }
    if (base + entry.length > offset) {
      size_t len = (entry.length + base) - offset;
      len = len > size ? size : len;

      zx_vaddr_t src_vaddr = entry.addr + offset - base;
      memcpy(dest, gpu_->phys_mem().as<void>(src_vaddr, len), len);

      dest += len;
      offset += len;
      size -= len;
    }
    base += entry.length;
  }
}

}  // namespace machina
