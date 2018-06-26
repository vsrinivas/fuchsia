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

constexpr uint32_t kRefreshRateHz = 30;
constexpr uint64_t kDisplayId = 1;

zx_status_t to_zx_status(uint32_t type) {
    LTRACEF("response type %#x\n", type);
    if (type != VIRTIO_GPU_RESP_OK_NODATA) {
        return ZX_ERR_NO_MEMORY;
    }
    return ZX_OK;
}

} // namespace

// DDK level ops

typedef struct imported_image {
    uint32_t resource_id;
    zx::pmt pmt;
} imported_image_t;

void GpuDevice::virtio_gpu_set_display_controller_cb(void* ctx, void* cb_ctx,
                                                     display_controller_cb_t* cb) {
    GpuDevice* gd = static_cast<GpuDevice*>(ctx);
    {
        fbl::AutoLock al(&gd->flush_lock_);
        gd->dc_cb_ = cb;
        gd->dc_cb_ctx_ = cb_ctx;
    }

    uint64_t disp_id = kDisplayId;
    cb->on_displays_changed(cb_ctx, &disp_id, 1, nullptr, 0);
}

zx_status_t GpuDevice::virtio_gpu_get_display_info(void* ctx, uint64_t id, display_info_t* info) {
    if (id != kDisplayId) {
        return ZX_ERR_INVALID_ARGS;
    }
    GpuDevice* gd = static_cast<GpuDevice*>(ctx);

    info->edid_present = false;
    info->panel.params.width = gd->pmode_.r.width;
    info->panel.params.height = gd->pmode_.r.height;
    info->panel.params.refresh_rate_e2 = kRefreshRateHz * 100;

    info->pixel_formats = &gd->supported_formats_;
    info->pixel_format_count = 1;
    return ZX_OK;
}

zx_status_t GpuDevice::virtio_gpu_import_vmo_image(void* ctx, image_t* image,
                                                   zx_handle_t vmo, size_t offset) {
    GpuDevice* gd = static_cast<GpuDevice*>(ctx);
    if (image->type != IMAGE_TYPE_SIMPLE) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::AllocChecker ac;
    auto import_data = fbl::make_unique_checked<imported_image_t>(&ac);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    unsigned pixel_size = ZX_PIXEL_FORMAT_BYTES(image->pixel_format);
    unsigned size = ROUNDUP(image->width * image->height * pixel_size, PAGE_SIZE);
    unsigned num_pages = size / PAGE_SIZE;
    zx_paddr_t paddr[num_pages];
    zx_status_t status = zx_bti_pin(gd->bti_.get(), ZX_BTI_PERM_READ, vmo, offset, size,
                                    paddr, num_pages, import_data->pmt.reset_and_get_address());
    if (status != ZX_OK) {
        return status;
    }

    for (unsigned i = 0; i < num_pages - 1; i++) {
        if (paddr[i] + PAGE_SIZE != paddr[i + 1]) {
            return ZX_ERR_INVALID_ARGS;
        }
    }

    status = gd->allocate_2d_resource(&import_data->resource_id, image->width, image->height);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to allocate 2d resource\n", gd->tag());
        return status;
    }

    status = gd->attach_backing(import_data->resource_id, paddr[0], size);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to attach backing store\n", gd->tag());
        return status;
    }

    image->handle = import_data.release();

    return ZX_OK;
}

void GpuDevice::virtio_gpu_release_image(void* ctx, image_t* image) {
    delete reinterpret_cast<imported_image_t*>(image->handle);
}

void GpuDevice::virtio_gpu_check_configuration(void* ctx,
                                               const display_config_t** display_configs,
                                               uint32_t** layer_cfg_results,
                                               uint32_t display_count) {
    GpuDevice* gd = static_cast<GpuDevice*>(ctx);
    if (display_count != 1) {
        ZX_DEBUG_ASSERT(display_count == 0);
        return;
    }
    ZX_DEBUG_ASSERT(display_configs[0]->display_id == kDisplayId);
    bool success;
    if (display_configs[0]->layer_count != 1) {
        success = display_configs[0]->layer_count == 0;
    } else {
        primary_layer_t* layer = &display_configs[0]->layers[0]->cfg.primary;
        frame_t frame = {
                .x_pos = 0, .y_pos = 0, .width = gd->pmode_.r.width, .height = gd->pmode_.r.height,
        };
        success = display_configs[0]->layers[0]->type == LAYER_PRIMARY
                && layer->transform_mode == FRAME_TRANSFORM_IDENTITY
                && layer->image.width == gd->pmode_.r.width
                && layer->image.height == gd->pmode_.r.height
                && memcmp(&layer->dest_frame, &frame, sizeof(frame_t)) == 0
                && memcmp(&layer->src_frame, &frame, sizeof(frame_t)) == 0
                && display_configs[0]->cc_flags == 0
                && layer->alpha_mode == ALPHA_DISABLE;
    }
    if (!success) {
        layer_cfg_results[0][0] = CLIENT_MERGE_BASE;
        for (unsigned i = 1; i < display_configs[0]->layer_count; i++) {
            layer_cfg_results[0][i] = CLIENT_MERGE_SRC;
        }
    }
}

void GpuDevice::virtio_gpu_apply_configuration(void* ctx, const display_config_t** display_configs,
                                               uint32_t display_count) {
    GpuDevice* gd = static_cast<GpuDevice*>(ctx);
    void* handle = display_count == 0 || display_configs[0]->layer_count == 0
            ? nullptr : display_configs[0]->layers[0]->cfg.primary.image.handle;

    {
        fbl::AutoLock al(&gd->flush_lock_);
        gd->current_fb_ = reinterpret_cast<imported_image_t*>(handle);
    }

    gd->Flush();
}

uint32_t GpuDevice::virtio_gpu_compute_linear_stride(void* ctx, uint32_t width,
                                                     zx_pixel_format_t format) {
    return width;
}

zx_status_t GpuDevice::virtio_gpu_allocate_vmo(void* ctx, uint64_t size, zx_handle_t* vmo_out) {
    GpuDevice* gd = static_cast<GpuDevice*>(ctx);
    return zx_vmo_create_contiguous(gd->bti().get(), size, 0, vmo_out);
}

GpuDevice::GpuDevice(zx_device_t* bus_device, zx::bti bti, fbl::unique_ptr<Backend> backend)
    : Device(bus_device, fbl::move(bti), fbl::move(backend)) {
    sem_init(&request_sem_, 0, 1);
    sem_init(&response_sem_, 0, 0);
    cnd_init(&flush_cond_);

    memset(&gpu_req_, 0, sizeof(gpu_req_));
}

GpuDevice::~GpuDevice() {
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
    zx_time_t next_deadline = zx_clock_get_monotonic();
    zx_time_t period = ZX_SEC(1) / kRefreshRateHz;
    for (;;) {
        zx_nanosleep(next_deadline);

        bool fb_change;
        {
            fbl::AutoLock al(&flush_lock_);
            fb_change = displayed_fb_ != current_fb_;
            displayed_fb_ = current_fb_;
        }

        LTRACEF("flushing\n");

        if (displayed_fb_) {
            zx_status_t status = transfer_to_host_2d(
                    displayed_fb_->resource_id, pmode_.r.width, pmode_.r.height);
            if (status != ZX_OK) {
                LTRACEF("failed to flush resource\n");
                continue;
            }

            status = flush_resource(displayed_fb_->resource_id, pmode_.r.width, pmode_.r.height);
            if (status != ZX_OK) {
                LTRACEF("failed to flush resource\n");
                continue;
            }
        }

        if (fb_change) {
            uint32_t res_id = displayed_fb_ ? displayed_fb_->resource_id : 0;
            zx_status_t status = set_scanout(pmode_id_, res_id, pmode_.r.width, pmode_.r.height);
            if (status != ZX_OK) {
                zxlogf(ERROR, "%s: failed to set scanout\n", tag());
                continue;
            }
        }

        {
            fbl::AutoLock al(&flush_lock_);
            if (dc_cb_) {
                void* handles[] = { static_cast<void*>(displayed_fb_) };
                dc_cb_->on_display_vsync(dc_cb_ctx_, kDisplayId,
                                         next_deadline, handles, displayed_fb_ != nullptr);
            }
        }
        next_deadline += period;
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

    // Run a worker thread to shove in flush events
    auto virtio_gpu_flusher_entry = [](void* arg) {
        static_cast<GpuDevice*>(arg)->virtio_gpu_flusher();
        return 0;
    };
    thrd_create_with_name(&flush_thread_, virtio_gpu_flusher_entry, this, "virtio-gpu-flusher");
    thrd_detach(flush_thread_);

    LTRACEF("publishing device\n");

    display_proto_ops_.set_display_controller_cb = virtio_gpu_set_display_controller_cb;
    display_proto_ops_.get_display_info = virtio_gpu_get_display_info;
    display_proto_ops_.import_vmo_image = virtio_gpu_import_vmo_image;
    display_proto_ops_.release_image = virtio_gpu_release_image;
    display_proto_ops_.check_configuration = virtio_gpu_check_configuration;
    display_proto_ops_.apply_configuration = virtio_gpu_apply_configuration;
    display_proto_ops_.compute_linear_stride = virtio_gpu_compute_linear_stride;
    display_proto_ops_.allocate_vmo = virtio_gpu_allocate_vmo;

    // Initialize the zx_device and publish us
    // Point the ctx of our DDK device at ourself
    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "virtio-gpu-display";
    args.ctx = this;
    args.ops = &device_ops_;
    args.proto_id = ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL;
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
