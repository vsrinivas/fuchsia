// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/gpu.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <fbl/intrusive_hash_table.h>
#include <fbl/unique_ptr.h>
#include <virtio/virtio_ids.h>
#include <zircon/process.h>

#include "third_party/skia/include/core/SkCanvas.h"

// A scanout that renders to a zircon framebuffer device.
class FramebufferScanout : public GpuScanout {
 public:
  // Create a scanout that owns a zircon framebuffer device.
  static zx_status_t Create(const char* path,
                            fbl::unique_ptr<GpuScanout>* out) {
    // Open framebuffer and get display info.
    int vfd = open(path, O_RDWR);
    if (vfd < 0)
      return ZX_ERR_NOT_FOUND;

    ioctl_display_get_fb_t fb;
    if (ioctl_display_get_fb(vfd, &fb) != sizeof(fb)) {
      close(vfd);
      return ZX_ERR_NOT_FOUND;
    }

    // Map framebuffer VMO.
    uintptr_t fbo;
    size_t size = fb.info.stride * fb.info.pixelsize * fb.info.height;
    zx_status_t status =
        zx_vmar_map(zx_vmar_root_self(), 0, fb.vmo, 0, size,
                    ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &fbo);
    if (status != ZX_OK) {
      close(vfd);
      return status;
    }

    // Wrap the framebuffer in an SkSurface so we can render using a canvas.
    SkImageInfo info =
        SkImageInfo::MakeN32Premul(fb.info.width, fb.info.height);
    size_t rowBytes = info.minRowBytes();
    sk_sp<SkSurface> surface = SkSurface::MakeRasterDirect(
        info, reinterpret_cast<void*>(fbo), rowBytes);

    auto scanout =
        fbl::make_unique<FramebufferScanout>(fbl::move(surface), vfd);
    *out = fbl::move(scanout);
    return ZX_OK;
  }

  FramebufferScanout(sk_sp<SkSurface>&& surface, int fd)
      : GpuScanout(fbl::move(surface)), fd_(fd) {}

  ~FramebufferScanout() {
    if (fd_ > 0)
      close(fd_);
  }

  void FlushRegion(const virtio_gpu_rect_t& rect) override {
    GpuScanout::FlushRegion(rect);
    ioctl_display_region_t fb_region = {
        .x = rect.x,
        .y = rect.y,
        .width = rect.width,
        .height = rect.height,
    };
    ioctl_display_flush_fb_region(fd_, &fb_region);
  }

 private:
  int fd_;
};

VirtioGpu::VirtioGpu(uintptr_t guest_physmem_addr, size_t guest_physmem_size)
    : VirtioDevice(VIRTIO_ID_GPU,
                   &config_,
                   sizeof(config_),
                   queues_,
                   VIRTIO_GPU_Q_COUNT,
                   guest_physmem_addr,
                   guest_physmem_size) {}

zx_status_t VirtioGpu::Init(const char* path) {
  fbl::unique_ptr<GpuScanout> gpu_scanout;
  zx_status_t status = FramebufferScanout::Create(path, &gpu_scanout);
  if (status != ZX_OK)
    return status;

  status = AddScanout(fbl::move(gpu_scanout));
  if (status != ZX_OK)
    return status;

  status = virtio_queue_poll(&queues_[VIRTIO_GPU_Q_CONTROLQ],
                             &VirtioGpu::QueueHandler, this);
  if (status != ZX_OK)
    return status;

  status = virtio_queue_poll(&queues_[VIRTIO_GPU_Q_CURSORQ],
                             &VirtioGpu::QueueHandler, this);
  if (status != ZX_OK)
    return status;

  return ZX_OK;
}

zx_status_t VirtioGpu::AddScanout(fbl::unique_ptr<GpuScanout> scanout) {
  if (scanout_ != nullptr)
    return ZX_ERR_ALREADY_EXISTS;

  config_.num_scanouts = 1;
  scanout_ = fbl::move(scanout);
  return ZX_OK;
}

zx_status_t VirtioGpu::QueueHandler(virtio_queue_t* queue,
                                    uint16_t head,
                                    uint32_t* used,
                                    void* ctx) {
  VirtioGpu* gpu = reinterpret_cast<VirtioGpu*>(ctx);
  return gpu->HandleGpuCommand(queue, head, used);
}

zx_status_t VirtioGpu::HandleGpuCommand(virtio_queue_t* queue,
                                        uint16_t head,
                                        uint32_t* used) {
  virtio_desc_t request_desc;
  virtio_queue_read_desc(queue, head, &request_desc);

  if (!request_desc.has_next)
    return ZX_ERR_INVALID_ARGS;
  auto header = reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(request_desc.addr);

  switch (header->type) {
    case VIRTIO_GPU_CMD_GET_DISPLAY_INFO: {
      virtio_desc_t response_desc;
      virtio_queue_read_desc(queue, request_desc.next, &response_desc);
      auto request =
          reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(request_desc.addr);
      auto response =
          reinterpret_cast<virtio_gpu_resp_display_info_t*>(response_desc.addr);
      GetDisplayInfo(request, response);
      return ZX_OK;
    }
    case VIRTIO_GPU_CMD_RESOURCE_CREATE_2D: {
      virtio_desc_t response_desc;
      virtio_queue_read_desc(queue, request_desc.next, &response_desc);
      auto request =
          reinterpret_cast<virtio_gpu_resource_create_2d_t*>(request_desc.addr);
      auto response =
          reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
      ResourceCreate2D(request, response);
      return ZX_OK;
    }
    case VIRTIO_GPU_CMD_SET_SCANOUT: {
      virtio_desc_t response_desc;
      virtio_queue_read_desc(queue, request_desc.next, &response_desc);
      auto request =
          reinterpret_cast<virtio_gpu_set_scanout_t*>(request_desc.addr);
      auto response =
          reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
      SetScanout(request, response);
      return ZX_OK;
    }
    case VIRTIO_GPU_CMD_RESOURCE_FLUSH: {
      virtio_desc_t response_desc;
      virtio_queue_read_desc(queue, request_desc.next, &response_desc);
      auto request =
          reinterpret_cast<virtio_gpu_resource_flush_t*>(request_desc.addr);
      auto response =
          reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
      ResourceFlush(request, response);
      return ZX_OK;
    }
    case VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D: {
      virtio_desc_t response_desc;
      virtio_queue_read_desc(queue, request_desc.next, &response_desc);
      auto request = reinterpret_cast<virtio_gpu_transfer_to_host_2d_t*>(
          request_desc.addr);
      auto response =
          reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
      TransferToHost2D(request, response);
      return ZX_OK;
    }
    case VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING: {
      virtio_desc_t response_desc;
      virtio_queue_read_desc(queue, request_desc.next, &response_desc);

      // This may or may not be on the same descriptor.
      virtio_gpu_mem_entry_t* mem_entries;
      if (response_desc.has_next) {
        mem_entries =
            reinterpret_cast<virtio_gpu_mem_entry_t*>(response_desc.addr);
        virtio_queue_read_desc(queue, response_desc.next, &response_desc);
      } else {
        uintptr_t addr = reinterpret_cast<uintptr_t>(request_desc.addr) +
                         sizeof(virtio_gpu_resource_attach_backing_t);
        mem_entries = reinterpret_cast<virtio_gpu_mem_entry_t*>(addr);
      }

      auto request = reinterpret_cast<virtio_gpu_resource_attach_backing_t*>(
          request_desc.addr);
      auto response =
          reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
      ResourceAttachBacking(request, mem_entries, response);
      return ZX_OK;
    }
    case VIRTIO_GPU_CMD_RESOURCE_UNREF: {
      virtio_desc_t response_desc;
      virtio_queue_read_desc(queue, request_desc.next, &response_desc);
      auto request =
          reinterpret_cast<virtio_gpu_resource_unref_t*>(request_desc.addr);
      auto response =
          reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
      ResourceUnref(request, response);
      return ZX_OK;
    }
    case VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING: {
      virtio_desc_t response_desc;
      virtio_queue_read_desc(queue, request_desc.next, &response_desc);
      auto request = reinterpret_cast<virtio_gpu_resource_detach_backing_t*>(
          request_desc.addr);
      auto response =
          reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
      ResourceDetachBacking(request, response);
      return ZX_OK;
    }
    // Not yet implemented.
    case VIRTIO_GPU_CMD_UPDATE_CURSOR:
    case VIRTIO_GPU_CMD_MOVE_CURSOR: {
      default:
        fprintf(stderr, "Unsupported GPU command %d\n", header->type);
        // ACK.
        virtio_desc_t response_desc;
        virtio_queue_read_desc(queue, request_desc.next, &response_desc);
        auto resp =
            reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
        resp->type = VIRTIO_GPU_RESP_ERR_UNSPEC;
        return ZX_ERR_NOT_SUPPORTED;
    }
  }
}

void VirtioGpu::GetDisplayInfo(const virtio_gpu_ctrl_hdr_t* request,
                               virtio_gpu_resp_display_info_t* response) {
  virtio_gpu_display_one_t* display = &response->pmodes[0];
  if (scanout_ == nullptr) {
    memset(display, 0, sizeof(*display));
    response->hdr.type = VIRTIO_GPU_RESP_ERR_UNSPEC;
    return;
  }

  display->enabled = 1;
  display->r.x = 0;
  display->r.y = 0;
  display->r.width = scanout_->width();
  display->r.height = scanout_->height();
  response->hdr.type = VIRTIO_GPU_RESP_OK_DISPLAY_INFO;
}

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

void VirtioGpu::ResourceCreate2D(const virtio_gpu_resource_create_2d_t* request,
                                 virtio_gpu_ctrl_hdr_t* response) {
  fbl::unique_ptr<GpuResource> res = GpuResource::Create(request, this);
  resources_.insert(fbl::move(res));
  response->type = VIRTIO_GPU_RESP_OK_NODATA;
}

void VirtioGpu::ResourceUnref(const virtio_gpu_resource_unref_t* request,
                              virtio_gpu_ctrl_hdr_t* response) {
  auto it = resources_.find(request->resource_id);
  if (it == resources_.end()) {
    response->type = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
    return;
  }
  resources_.erase(it);
  response->type = VIRTIO_GPU_RESP_OK_NODATA;
}

void VirtioGpu::SetScanout(const virtio_gpu_set_scanout_t* request,
                           virtio_gpu_ctrl_hdr_t* response) {
  if (request->resource_id == 0) {
    // Resource ID 0 is a special case and means the provided scanout
    // should be disabled.
    scanout_->SetResource(nullptr, request);
    response->type = VIRTIO_GPU_RESP_OK_NODATA;
    return;
  }
  if (request->scanout_id != 0 || scanout_ == nullptr) {
    // Only a single scanout is supported.
    response->type = VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID;
    return;
  }

  auto res = resources_.find(request->resource_id);
  if (res == resources_.end()) {
    response->type = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
    return;
  }
  scanout_->SetResource(&*res, request);

  response->type = VIRTIO_GPU_RESP_OK_NODATA;
}

void VirtioGpu::ResourceAttachBacking(
    const virtio_gpu_resource_attach_backing_t* request,
    const virtio_gpu_mem_entry_t* mem_entries,
    virtio_gpu_ctrl_hdr_t* response) {
  auto it = resources_.find(request->resource_id);
  if (it == resources_.end()) {
    response->type = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
    return;
  }
  response->type = it->AttachBacking(mem_entries, request->nr_entries);
}

void VirtioGpu::ResourceDetachBacking(
    const virtio_gpu_resource_detach_backing_t* request,
    virtio_gpu_ctrl_hdr_t* response) {
  auto it = resources_.find(request->resource_id);
  if (it == resources_.end()) {
    response->type = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
    return;
  }
  response->type = it->DetachBacking();
}

void VirtioGpu::TransferToHost2D(
    const virtio_gpu_transfer_to_host_2d_t* request,
    virtio_gpu_ctrl_hdr_t* response) {
  auto it = resources_.find(request->resource_id);
  if (it == resources_.end()) {
    response->type = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
    return;
  }
  response->type = it->TransferToHost2D(request);
}

void VirtioGpu::ResourceFlush(const virtio_gpu_resource_flush_t* request,
                              virtio_gpu_ctrl_hdr_t* response) {
  auto it = resources_.find(request->resource_id);
  if (it == resources_.end()) {
    response->type = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
    return;
  }
  response->type = it->Flush(request);
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

void GpuScanout::FlushRegion(const virtio_gpu_rect_t& rect) {
  GpuResource* res = resource_;
  if (res == nullptr) {
    return;
  }
  SkCanvas* canvas = surface_->getCanvas();
  SkRect surface_rect = SkRect::MakeIWH(surface_->width(), surface_->height());
  canvas->drawBitmapRect(res->bitmap(), rect_, surface_rect, nullptr);
}

zx_status_t GpuScanout::SetResource(GpuResource* res,
                                    const virtio_gpu_set_scanout_t* request) {
  GpuResource* old_res = resource_;
  resource_ = res;
  if (resource_ == nullptr) {
    if (old_res != nullptr)
      old_res->DetachFromScanout();
    return ZX_OK;
  }
  resource_->AttachToScanout(this);
  rect_ = SkRect::MakeXYWH(
      SkIntToScalar(request->r.x), SkIntToScalar(request->r.y),
      SkIntToScalar(request->r.width), SkIntToScalar(request->r.height));
  return ZX_OK;
}
