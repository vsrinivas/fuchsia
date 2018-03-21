// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu.h"

#include <inttypes.h>
#include <string.h>
#include <sys/param.h>

#include <ddk/debug.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <zircon/compiler.h>

#include "trace.h"
#include "virtio_gpu.h"

#define LOCAL_TRACE 0

namespace virtio {

namespace {

zx_status_t to_zx_status(uint32_t type) {
    LTRACEF("response type %#x\n", type);
    if (type != VIRTIO_GPU_RESP_OK_NODATA) {
        return ZX_ERR_NO_MEMORY;
    }
    return ZX_OK;
}

} // namespace

// DDK level ops

// Queue an iotxn. iotxn's are always completed by its complete() op
zx_status_t GpuDevice::virtio_gpu_set_mode(void* ctx, zx_display_info_t* info) {
    GpuDevice* gd = static_cast<GpuDevice*>(ctx);

    LTRACEF("dev %p, info %p\n", gd, info);

    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t GpuDevice::virtio_gpu_get_mode(void* ctx, zx_display_info_t* info) {
    GpuDevice* gd = static_cast<GpuDevice*>(ctx);

    LTRACEF("dev %p, info %p\n", gd, info);

    *info = {};

    auto pmode = gd->pmode();

    info->format = ZX_PIXEL_FORMAT_RGB_x888;
    info->width = pmode->r.width;
    info->height = pmode->r.height;
    info->stride = pmode->r.width;
    info->pixelsize = 4;
    info->flags = ZX_DISPLAY_FLAG_HW_FRAMEBUFFER;

    return ZX_OK;
}

zx_status_t GpuDevice::virtio_gpu_get_framebuffer(void* ctx, void** framebuffer) {
    GpuDevice* gd = static_cast<GpuDevice*>(ctx);

    LTRACEF("dev %p, framebuffer %p\n", gd, framebuffer);

    void* fb = gd->framebuffer();
    if (!fb)
        return ZX_ERR_NOT_SUPPORTED;

    *framebuffer = fb;
    return ZX_OK;
}

void GpuDevice::virtio_gpu_flush(void* ctx) {
    GpuDevice* gd = static_cast<GpuDevice*>(ctx);

    LTRACEF("dev %p\n", gd);

    gd->Flush();
}

GpuDevice::GpuDevice(zx_device_t* bus_device, zx::bti bti, fbl::unique_ptr<Backend> backend)
    : Device(bus_device, fbl::move(bti), fbl::move(backend)) {
    sem_init(&request_sem_, 0, 1);
    sem_init(&response_sem_, 0, 0);
    cnd_init(&flush_cond_);

    memset(&fb_, 0, sizeof(fb_));
    memset(&gpu_req_, 0, sizeof(gpu_req_));
}

GpuDevice::~GpuDevice() {
    io_buffer_release(&fb_);
    io_buffer_release(&gpu_req_);

    // TODO: clean up allocated physical memory
    sem_destroy(&request_sem_);
    sem_destroy(&response_sem_);
    cnd_destroy(&flush_cond_);
}

template <typename RequestType, typename ResponseType>
void GpuDevice::send_command_response(const RequestType* cmd, ResponseType** res) {
    size_t cmd_len = sizeof(RequestType);
    size_t res_len = sizeof(ResponseType);
    LTRACEF("dev %p, cmd %p, cmd_len %zu, res %p, res_len %zu\n", this, cmd, cmd_len, res, res_len);

    // Keep this single message at a time
    sem_wait(&request_sem_);
    fbl::MakeAutoCall([this]() { sem_post(&request_sem_); });

    uint16_t i;
    struct vring_desc* desc = vring_.AllocDescChain(2, &i);
    ZX_ASSERT(desc);

    void* gpu_req_base = io_buffer_virt(&gpu_req_);
    zx_paddr_t gpu_req_pa = io_buffer_phys(&gpu_req_);

    memcpy(gpu_req_base, cmd, cmd_len);

    desc->addr = gpu_req_pa;
    desc->len = static_cast<uint32_t>(cmd_len);
    desc->flags = VRING_DESC_F_NEXT;

    // Set the second descriptor to the response with the write bit set
    desc = vring_.DescFromIndex(desc->next);
    ZX_ASSERT(desc);

    *res = reinterpret_cast<ResponseType*>(static_cast<uint8_t*>(gpu_req_base) + cmd_len);
    zx_paddr_t res_phys = gpu_req_pa + cmd_len;
    memset(*res, 0, res_len);

    desc->addr = res_phys;
    desc->len = static_cast<uint32_t>(res_len);
    desc->flags = VRING_DESC_F_WRITE;

    // Submit the transfer & wait for the response
    vring_.SubmitChain(i);
    vring_.Kick();
    sem_wait(&response_sem_);
}

zx_status_t GpuDevice::get_display_info() {
    LTRACEF("dev %p\n", this);

    // Construct the get display info message
    virtio_gpu_ctrl_hdr req;
    memset(&req, 0, sizeof(req));
    req.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;

    // Send the message and get a response
    virtio_gpu_resp_display_info* info;
    send_command_response(&req, &info);
    if (info->hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO) {
        return ZX_ERR_NOT_FOUND;
    }

    // We got a response
    LTRACEF("response:\n");
    for (int i = 0; i < VIRTIO_GPU_MAX_SCANOUTS; i++) {
        if (info->pmodes[i].enabled) {
            LTRACEF("%u: x %u y %u w %u h %u flags 0x%x\n", i,
                    info->pmodes[i].r.x, info->pmodes[i].r.y, info->pmodes[i].r.width, info->pmodes[i].r.height,
                    info->pmodes[i].flags);
            if (pmode_id_ < 0) {
                // Save the first valid pmode we see
                memcpy(&pmode_, &info->pmodes[i], sizeof(pmode_));
                pmode_id_ = i;
            }
        }
    }

    return ZX_OK;
}

zx_status_t GpuDevice::allocate_2d_resource(uint32_t* resource_id, uint32_t width, uint32_t height) {
    LTRACEF("dev %p\n", this);

    ZX_ASSERT(resource_id);

    // Construct the request
    virtio_gpu_resource_create_2d req;
    memset(&req, 0, sizeof(req));

    req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    req.resource_id = next_resource_id_++;
    *resource_id = req.resource_id;
    req.format = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
    req.width = width;
    req.height = height;

    // Send the command and get a response
    struct virtio_gpu_ctrl_hdr* res;
    send_command_response(&req, &res);

    return to_zx_status(res->type);
}

zx_status_t GpuDevice::attach_backing(uint32_t resource_id, zx_paddr_t ptr, size_t buf_len) {
    LTRACEF("dev %p, resource_id %u, ptr %#" PRIxPTR ", buf_len %zu\n", this, resource_id, ptr, buf_len);

    ZX_ASSERT(ptr);

    // Construct the request
    struct {
        struct virtio_gpu_resource_attach_backing req;
        struct virtio_gpu_mem_entry mem;
    } req;
    memset(&req, 0, sizeof(req));

    req.req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    req.req.resource_id = resource_id;
    req.req.nr_entries = 1;

    req.mem.addr = ptr;
    req.mem.length = (uint32_t)buf_len;

    // Send the command and get a response
    struct virtio_gpu_ctrl_hdr* res;
    send_command_response(&req, &res);
    return to_zx_status(res->type);
}

zx_status_t GpuDevice::set_scanout(uint32_t scanout_id, uint32_t resource_id, uint32_t width, uint32_t height) {
    LTRACEF("dev %p, scanout_id %u, resource_id %u, width %u, height %u\n", this, scanout_id, resource_id, width, height);

    // Construct the request
    virtio_gpu_set_scanout req;
    memset(&req, 0, sizeof(req));

    req.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    req.r.x = req.r.y = 0;
    req.r.width = width;
    req.r.height = height;
    req.scanout_id = scanout_id;
    req.resource_id = resource_id;

    // Send the command and get a response
    virtio_gpu_ctrl_hdr* res;
    send_command_response(&req, &res);
    return to_zx_status(res->type);
}

zx_status_t GpuDevice::flush_resource(uint32_t resource_id, uint32_t width, uint32_t height) {
    LTRACEF("dev %p, resource_id %u, width %u, height %u\n", this, resource_id, width, height);

    // Construct the request
    virtio_gpu_resource_flush req;
    memset(&req, 0, sizeof(req));

    req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    req.r.x = req.r.y = 0;
    req.r.width = width;
    req.r.height = height;
    req.resource_id = resource_id;

    // Send the command and get a response
    virtio_gpu_ctrl_hdr* res;
    send_command_response(&req, &res);
    return to_zx_status(res->type);
}

zx_status_t GpuDevice::transfer_to_host_2d(uint32_t resource_id, uint32_t width, uint32_t height) {
    LTRACEF("dev %p, resource_id %u, width %u, height %u\n", this, resource_id, width, height);

    // Construct the request
    virtio_gpu_transfer_to_host_2d req;
    memset(&req, 0, sizeof(req));

    req.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    req.r.x = req.r.y = 0;
    req.r.width = width;
    req.r.height = height;
    req.offset = 0;
    req.resource_id = resource_id;

    // Send the command and get a response
    virtio_gpu_ctrl_hdr* res;
    send_command_response(&req, &res);
    return to_zx_status(res->type);
}

void GpuDevice::Flush() {
    fbl::AutoLock al(&flush_lock_);
    flush_pending_ = true;
    cnd_signal(&flush_cond_);
}

void GpuDevice::virtio_gpu_flusher() {
    LTRACE_ENTRY;
    for (;;) {
        {
            fbl::AutoLock al(&flush_lock_);
            while (!flush_pending_)
                cnd_wait(&flush_cond_, flush_lock_.GetInternal());
            flush_pending_ = false;
        }

        LTRACEF("flushing\n");

        zx_status_t status = transfer_to_host_2d(display_resource_id_, pmode_.r.width, pmode_.r.height);
        if (status != ZX_OK) {
            LTRACEF("failed to flush resource\n");
            continue;
        }

        status = flush_resource(display_resource_id_, pmode_.r.width, pmode_.r.height);
        if (status != ZX_OK) {
            LTRACEF("failed to flush resource\n");
            continue;
        }
    }
}

zx_status_t GpuDevice::virtio_gpu_start() {

    LTRACEF("dev %p\n", this);

    // Get the display info and see if we find a valid pmode
    zx_status_t status = get_display_info();
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to get display info\n", tag());
        return status;
    }

    if (pmode_id_ < 0) {
        zxlogf(ERROR, "%s: failed to find a pmode, exiting\n", tag());
        return ZX_ERR_NOT_FOUND;
    }

    printf("virtio-gpu: found display x %u y %u w %u h %u flags 0x%x\n",
           pmode_.r.x, pmode_.r.y, pmode_.r.width, pmode_.r.height,
           pmode_.flags);

    // Allocate a resource
    status = allocate_2d_resource(&display_resource_id_, pmode_.r.width, pmode_.r.height);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to allocate 2d resource\n", tag());
        return status;
    }

    // Attach a backing store to the resource
    size_t len = pmode_.r.width * pmode_.r.height * 4;

    status = io_buffer_init(&fb_, bti_.get(), len, IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to allocate framebuffer, wanted 0x%zx bytes\n", tag(), len);
        return ZX_ERR_NO_MEMORY;
    }

    LTRACEF("framebuffer at %p, 0x%zx bytes\n", io_buffer_virt(&fb_), len);

    status = attach_backing(display_resource_id_, io_buffer_phys(&fb_), len);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to attach backing store\n", tag());
        return status;
    }

    // Attach this resource as a scanout
    status = set_scanout(pmode_id_, display_resource_id_, pmode_.r.width, pmode_.r.height);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to set scanout\n", tag());
        return status;
    }

    // Run a worker thread to shove in flush events
    auto virtio_gpu_flusher_entry = [](void* arg) {
        static_cast<GpuDevice*>(arg)->virtio_gpu_flusher();
        return 0;
    };
    thrd_create_with_name(&flush_thread_, virtio_gpu_flusher_entry, this, "virtio-gpu-flusher");
    thrd_detach(flush_thread_);

    LTRACEF("publishing device\n");

    display_proto_ops_.set_mode = virtio_gpu_set_mode;
    display_proto_ops_.get_mode = virtio_gpu_get_mode;
    display_proto_ops_.get_framebuffer = virtio_gpu_get_framebuffer;
    display_proto_ops_.flush = virtio_gpu_flush;

    // Initialize the zx_device and publish us
    // Point the ctx of our DDK device at ourself
    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "virtio-gpu";
    args.ctx = this;
    args.ops = &device_ops_;
    args.proto_id = ZX_PROTOCOL_DISPLAY;
    args.proto_ops = &display_proto_ops_;

    status = device_add(bus_device_, &args, &bus_device_);
    if (status != ZX_OK) {
        device_ = nullptr;
        return status;
    }

    LTRACE_EXIT;
    return ZX_OK;
}

zx_status_t GpuDevice::Init() {
    LTRACE_ENTRY;

    DeviceReset();

    struct virtio_gpu_config config;
    CopyDeviceConfig(&config, sizeof(config));
    LTRACEF("events_read 0x%x\n", config.events_read);
    LTRACEF("events_clear 0x%x\n", config.events_clear);
    LTRACEF("num_scanouts 0x%x\n", config.num_scanouts);
    LTRACEF("reserved 0x%x\n", config.reserved);

    // Ack and set the driver status bit
    DriverStatusAck();

    // XXX check features bits and ack/nak them

    // Allocate the main vring
    zx_status_t status = vring_.Init(0, 16);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to allocate vring\n", tag());
        return status;
    }

    // Allocate a GPU request
    status = io_buffer_init(&gpu_req_, bti_.get(), PAGE_SIZE, IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: cannot alloc gpu_req buffers %d\n", tag(), status);
        return status;
    }

    LTRACEF("allocated gpu request at %p, physical address %#" PRIxPTR "\n",
            io_buffer_virt(&gpu_req_), io_buffer_phys(&gpu_req_));

    StartIrqThread();
    DriverStatusOk();

    // Start a worker thread that runs through a sequence to finish initializing the GPU
    auto virtio_gpu_start_entry = [](void* arg) {
        return static_cast<GpuDevice*>(arg)->virtio_gpu_start();
    };
    thrd_create_with_name(&start_thread_, virtio_gpu_start_entry, this, "virtio-gpu-starter");
    thrd_detach(start_thread_);

    return ZX_OK;
}

void GpuDevice::IrqRingUpdate() {
    LTRACE_ENTRY;

    // Parse our descriptor chain, add back to the free queue
    auto free_chain = [this](vring_used_elem* used_elem) {
        uint16_t i = static_cast<uint16_t>(used_elem->id);
        struct vring_desc* desc = vring_.DescFromIndex(i);
        for (;;) {
            int next;

            if (desc->flags & VRING_DESC_F_NEXT) {
                next = desc->next;
            } else {
                // End of chain
                next = -1;
            }

            vring_.FreeDesc(i);

            if (next < 0) {
                break;
            }
            i = static_cast<uint16_t>(next);
            desc = vring_.DescFromIndex(i);
        }
        // Notify the request thread
        sem_post(&response_sem_);
    };

    // Tell the ring to find free chains and hand it back to our lambda
    vring_.IrqRingUpdate(free_chain);
}

void GpuDevice::IrqConfigChange() {
    LTRACE_ENTRY;
}

} // namespace virtio
