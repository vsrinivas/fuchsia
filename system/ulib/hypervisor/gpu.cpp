// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hypervisor/gpu.h>

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <fbl/auto_lock.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/unique_ptr.h>
#include <hypervisor/vcpu.h>
#include <hypervisor/virtio.h>
#include <virtio/gpu.h>
#include <virtio/virtio.h>
#include <virtio/virtio_ids.h>
#include <zircon/pixelformat.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

// Convert virtio gpu formats to zircon formats.
static uint32_t guest_pixel_format(uint32_t format) {
    switch (format) {
    case VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM:
        return ZX_PIXEL_FORMAT_ARGB_8888;
    case VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM:
        return ZX_PIXEL_FORMAT_RGB_x888;
    default:
        fprintf(stderr, "virtio format %#x not known\n", format);
        return ZX_PIXEL_FORMAT_NONE;
    }
}

VirtioGpu::VirtioGpu(uintptr_t guest_physmem_addr, size_t guest_physmem_size)
    : VirtioDevice(VIRTIO_ID_GPU, &config_, sizeof(config_), queues_, VIRTIO_GPU_Q_COUNT,
                   guest_physmem_addr, guest_physmem_size) {
}

zx_status_t VirtioGpu::Init(const char* path) {
    zx_status_t status = GpuScanout::Create(path, &scanout_);
    if (status != ZX_OK)
        return status;
    config_.num_scanouts = 1;

    status = virtio_queue_poll(&queues_[VIRTIO_GPU_Q_CONTROLQ], &VirtioGpu::QueueHandler, this);
    if (status != ZX_OK)
        return status;

    status = virtio_queue_poll(&queues_[VIRTIO_GPU_Q_CURSORQ], &VirtioGpu::QueueHandler, this);
    if (status != ZX_OK)
        return status;

    return ZX_OK;
}

zx_status_t VirtioGpu::QueueHandler(virtio_queue_t* queue, uint16_t head, uint32_t* used,
                                    void* ctx) {
    VirtioGpu* gpu = reinterpret_cast<VirtioGpu*>(ctx);
    return gpu->HandleGpuCommand(queue, head, used);
}

zx_status_t VirtioGpu::HandleGpuCommand(virtio_queue_t* queue, uint16_t head, uint32_t* used) {
    virtio_desc_t request_desc;
    virtio_queue_read_desc(queue, head, &request_desc);

    if (!request_desc.has_next)
        return ZX_ERR_INVALID_ARGS;
    auto header = reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(request_desc.addr);

    switch (header->type) {
    case VIRTIO_GPU_CMD_GET_DISPLAY_INFO: {
        virtio_desc_t response_desc;
        virtio_queue_read_desc(queue, request_desc.next, &response_desc);
        auto request = reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(request_desc.addr);
        auto response = reinterpret_cast<virtio_gpu_resp_display_info_t*>(response_desc.addr);
        GetDisplayInfo(request, response);
        return ZX_OK;
    }
    case VIRTIO_GPU_CMD_RESOURCE_CREATE_2D: {
        virtio_desc_t response_desc;
        virtio_queue_read_desc(queue, request_desc.next, &response_desc);
        auto request = reinterpret_cast<virtio_gpu_resource_create_2d_t*>(request_desc.addr);
        auto response = reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
        ResourceCreate2D(request, response);
        return ZX_OK;
    }
    case VIRTIO_GPU_CMD_SET_SCANOUT: {
        virtio_desc_t response_desc;
        virtio_queue_read_desc(queue, request_desc.next, &response_desc);
        auto request = reinterpret_cast<virtio_gpu_set_scanout_t*>(request_desc.addr);
        auto response = reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
        SetScanout(request, response);
        return ZX_OK;
    }
    case VIRTIO_GPU_CMD_RESOURCE_FLUSH: {
        virtio_desc_t response_desc;
        virtio_queue_read_desc(queue, request_desc.next, &response_desc);
        auto request = reinterpret_cast<virtio_gpu_resource_flush_t*>(request_desc.addr);
        auto response = reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
        ResourceFlush(request, response);
        return ZX_OK;
    }
    case VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D: {
        virtio_desc_t response_desc;
        virtio_queue_read_desc(queue, request_desc.next, &response_desc);
        auto request = reinterpret_cast<virtio_gpu_transfer_to_host_2d_t*>(request_desc.addr);
        auto response = reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
        TransferToHost2D(request, response);
        return ZX_OK;
    }
    case VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING: {
        virtio_desc_t response_desc;
        virtio_queue_read_desc(queue, request_desc.next, &response_desc);

        // This may or may not be on the same descriptor.
        virtio_gpu_mem_entry_t* mem_entries;
        if (response_desc.has_next) {
            mem_entries = reinterpret_cast<virtio_gpu_mem_entry_t*>(response_desc.addr);
            virtio_queue_read_desc(queue, response_desc.next, &response_desc);
        } else {
            uintptr_t addr = reinterpret_cast<uintptr_t>(request_desc.addr) +
                             sizeof(virtio_gpu_resource_attach_backing_t);
            mem_entries = reinterpret_cast<virtio_gpu_mem_entry_t*>(addr);
        }

        auto request = reinterpret_cast<virtio_gpu_resource_attach_backing_t*>(request_desc.addr);
        auto response = reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
        ResourceAttachBacking(request, mem_entries, response);
        return ZX_OK;
    }
    case VIRTIO_GPU_CMD_RESOURCE_UNREF: {
        virtio_desc_t response_desc;
        virtio_queue_read_desc(queue, request_desc.next, &response_desc);
        auto request = reinterpret_cast<virtio_gpu_resource_unref_t*>(request_desc.addr);
        auto response = reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
        ResourceUnref(request, response);
        return ZX_OK;
    }
    case VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING: {
        virtio_desc_t response_desc;
        virtio_queue_read_desc(queue, request_desc.next, &response_desc);
        auto request = reinterpret_cast<virtio_gpu_resource_detach_backing_t*>(request_desc.addr);
        auto response = reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
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
        auto resp = reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
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

void VirtioGpu::ResourceCreate2D(const virtio_gpu_resource_create_2d_t* request,
                                 virtio_gpu_ctrl_hdr_t* response) {
    auto res = fbl::make_unique<GpuResource>(this, request);
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
        response->type = VIRTIO_GPU_RESP_OK_NODATA;
        return;
    }
    if (request->scanout_id != 0) {
        // Only a single scanout is supported.
        response->type = VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID;
        return;
    }

    auto it = resources_.find(request->resource_id);
    if (it == resources_.end()) {
        response->type = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
        return;
    }

    response->type = it->SetScanout(scanout_.get());
}

void VirtioGpu::ResourceAttachBacking(const virtio_gpu_resource_attach_backing_t* request,
                                      const virtio_gpu_mem_entry_t* mem_entries,
                                      virtio_gpu_ctrl_hdr_t* response) {
    auto it = resources_.find(request->resource_id);
    if (it == resources_.end()) {
        response->type = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
        return;
    }
    response->type = it->AttachBacking(mem_entries, request->nr_entries);
}

void VirtioGpu::ResourceDetachBacking(const virtio_gpu_resource_detach_backing_t* request,
                                      virtio_gpu_ctrl_hdr_t* response) {
    auto it = resources_.find(request->resource_id);
    if (it == resources_.end()) {
        response->type = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
        return;
    }
    response->type = it->DetachBacking();
}

void VirtioGpu::TransferToHost2D(const virtio_gpu_transfer_to_host_2d_t* request,
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

zx_status_t GpuScanout::Create(const char* path, fbl::unique_ptr<GpuScanout>* out) {
    // Open framebuffer and get display info.
    int vfd = open(path, O_RDWR);
    if (vfd < 0)
        return ZX_ERR_NOT_FOUND;
    auto scanout = fbl::make_unique<GpuScanout>();

    scanout->fd = vfd;
    if (ioctl_display_get_fb(scanout->fd, &scanout->fb) != sizeof(scanout->fb))
        return ZX_ERR_NOT_FOUND;

    // Map framebuffer VMO.
    uintptr_t fbo;
    size_t size = scanout->fb.info.stride * scanout->fb.info.pixelsize * scanout->fb.info.height;
    zx_status_t status = zx_vmar_map(zx_vmar_root_self(), 0, scanout->fb.vmo, 0, size,
                                     ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &fbo);
    if (status != ZX_OK)
        return ZX_ERR_NOT_FOUND;

    scanout->buffer = reinterpret_cast<uint8_t*>(fbo);
    scanout->buffer_len = size;
    *out = fbl::move(scanout);
    return ZX_OK;
}

GpuResource::GpuResource(VirtioGpu* gpu, const virtio_gpu_resource_create_2d_t* args)
    : gpu_(gpu), res_id_(args->resource_id), format_(guest_pixel_format(args->format)) {}

virtio_gpu_ctrl_type GpuResource::AttachBacking(const virtio_gpu_mem_entry_t* mem_entries,
                                                uint32_t num_entries) {
    for (int i = num_entries - 1; i >= 0; --i) {
        const virtio_gpu_mem_entry_t* entry = &mem_entries[i];
        backing_.push_front(fbl::make_unique<BackingPages>(entry->addr, entry->length));
    }
    return VIRTIO_GPU_RESP_OK_NODATA;
}

virtio_gpu_ctrl_type GpuResource::DetachBacking() {
    backing_.clear();
    return VIRTIO_GPU_RESP_OK_NODATA;
}

virtio_gpu_ctrl_type GpuResource::TransferToHost2D(const virtio_gpu_transfer_to_host_2d_t* request) {
    if (scanout_ == nullptr)
        return VIRTIO_GPU_RESP_ERR_UNSPEC;

    // No transposition is currently supported.
    if (format_ != scanout_->fb.info.format && !pixel_format_warning_) {
        pixel_format_warning_ = true;
        fprintf(stderr, "virtio-gpu: Guest/Host pixel format mismatch. (%#x vs %#x)\n",
                format_, scanout_->fb.info.format);
        return VIRTIO_GPU_RESP_ERR_UNSPEC;
    }

    const uint8_t bpp = 4;
    // Optimize for copying a contiguous region.
    uint32_t stride = scanout_->width() * bpp;
    if (request->offset == 0 && request->r.x == 0 && request->r.y == 0 &&
        request->r.width == scanout_->height()) {
        CopyBytes(0, scanout_->buffer, stride * scanout_->height());
        return VIRTIO_GPU_RESP_OK_NODATA;
    }

    // line-by-line copy.
    uint32_t linesize = request->r.width * 4;
    for (uint32_t line = 0; line < request->r.height; ++line) {
        uint64_t src_offset = request->offset + stride * line;
        size_t size = ((request->r.y + line) * stride) + (request->r.x * bpp);

        CopyBytes(src_offset, scanout_->buffer + size, linesize);
    }
    return VIRTIO_GPU_RESP_OK_NODATA;
}

virtio_gpu_ctrl_type GpuResource::Flush(const virtio_gpu_resource_flush_t* request) {
    if (scanout_ == nullptr)
        return VIRTIO_GPU_RESP_ERR_UNSPEC;

    ioctl_display_region_t fb_region = {
        .x = request->r.x,
        .y = request->r.y,
        .width = request->r.width,
        .height = request->r.height,
    };
    ioctl_display_flush_fb_region(scanout_->fd, &fb_region);
    return VIRTIO_GPU_RESP_OK_NODATA;
}

virtio_gpu_ctrl_type GpuResource::SetScanout(GpuScanout* scanout) {
    scanout_ = scanout;
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
