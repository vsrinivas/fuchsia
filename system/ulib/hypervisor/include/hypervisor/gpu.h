// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <fbl/intrusive_hash_table.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <hypervisor/virtio.h>
#include <virtio/gpu.h>
#include <zircon/compiler.h>
#include <zircon/device/display.h>
#include <zircon/types.h>

#define VIRTIO_GPU_Q_CONTROLQ 0
#define VIRTIO_GPU_Q_CURSORQ 1
#define VIRTIO_GPU_Q_COUNT 2

class VirtioGpu;

using ResourceId = uint32_t;
using ScanoutId = uint32_t;

/* A scanout represents a display that GPU resources can be rendered to.
 *
 * Each scanout will own a single device under /dev/class/framebuffer/
 */
struct GpuScanout {
    static zx_status_t Create(const char* framebuffer, fbl::unique_ptr<GpuScanout>* out);

    uint32_t height() { return fb.info.height; }
    uint32_t width() { return fb.info.width; }

    int fd = 0;
    ioctl_display_get_fb_t fb = {};
    uint8_t* buffer = nullptr;
    size_t buffer_len = 0;
};

/* A resource corresponds to a single display buffer. */
class GpuResource : public fbl::SinglyLinkedListable<fbl::unique_ptr<GpuResource>> {
public:
    // The driver will provide a scatter-gather list of memory pages to back
    // the framebuffer in guest physical memory.
    struct BackingPages : public fbl::SinglyLinkedListable<fbl::unique_ptr<BackingPages>> {
        uint64_t addr;
        uint32_t length;

        BackingPages(uint64_t addr_, uint32_t length_)
            : addr(addr_), length(length_) {}
    };

    using HashTable = fbl::HashTable<ResourceId, fbl::unique_ptr<GpuResource>>;

    GpuResource(VirtioGpu* gpu, const virtio_gpu_resource_create_2d_t* args);

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
    virtio_gpu_ctrl_type TransferToHost2D(const virtio_gpu_transfer_to_host_2d_t* request);

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
    GpuScanout* scanout_;
    ResourceId res_id_;
    uint32_t format_;
    bool pixel_format_warning_ = false;
    fbl::SinglyLinkedList<fbl::unique_ptr<BackingPages>> backing_;
};

/* Virtio 2D GPU device. */
class VirtioGpu : public VirtioDevice {
public:
    VirtioGpu(uintptr_t guest_physmem_addr, size_t guest_physmem_size);
    ~VirtioGpu() override = default;

    // Opens the framebuffer device located at |path| and starts processing
    // any descriptors that become available in the queues.
    //
    // Currently only a single framebuffer is supported.
    zx_status_t Init(const char* path);

protected:
    static zx_status_t QueueHandler(virtio_queue_t* queue, uint16_t head, uint32_t* used,
                                    void* ctx);

    zx_status_t HandleGpuCommand(virtio_queue_t* queue, uint16_t head, uint32_t* used);

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
    void SetScanout(const virtio_gpu_set_scanout_t* request, virtio_gpu_ctrl_hdr_t* response);

    // VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING
    void ResourceAttachBacking(const virtio_gpu_resource_attach_backing_t* request,
                               const virtio_gpu_mem_entry_t* mem_entries,
                               virtio_gpu_ctrl_hdr_t* response);

    // VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING
    void ResourceDetachBacking(const virtio_gpu_resource_detach_backing_t* request,
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
