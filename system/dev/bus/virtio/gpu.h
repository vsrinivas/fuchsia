// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <semaphore.h>
#include <stdlib.h>

#include <ddk/protocol/display-controller.h>
#include <fbl/unique_ptr.h>
#include <zircon/compiler.h>

#include "device.h"
#include "ring.h"
#include "virtio_gpu.h"

namespace virtio {

class Ring;

class GpuDevice : public Device {
public:
    GpuDevice(zx_device_t* device, zx::bti bti, fbl::unique_ptr<Backend> backend);
    virtual ~GpuDevice();

    zx_status_t Init() override;

    void IrqRingUpdate() override;
    void IrqConfigChange() override;

    const virtio_gpu_resp_display_info::virtio_gpu_display_one* pmode() const { return &pmode_; }

    void Flush();

    const char* tag() const override { return "virtio-gpu"; };

private:
    // DDK driver hooks
    static void virtio_gpu_set_display_controller_cb(
            void* ctx, void* cb_ctx, display_controller_cb_t* cb);
    static zx_status_t virtio_gpu_get_display_info(void* ctx, uint64_t id, display_info_t* info);
    static zx_status_t virtio_gpu_import_vmo_image(
            void* ctx, image_t* image, zx_handle_t vmo, size_t offset);
    static void virtio_gpu_release_image(void* ctx, image_t* image);
    static void virtio_gpu_check_configuration(
            void* ctx, const display_config_t** display_configs,
            uint32_t* display_cfg_result, uint32_t** layer_cfg_result,
            uint32_t display_count);
    static void virtio_gpu_apply_configuration(
            void* ctx, const display_config_t** display_configs, uint32_t display_count);
    static uint32_t virtio_gpu_compute_linear_stride(
            void* ctx, uint32_t width, zx_pixel_format_t format);
    static zx_status_t virtio_gpu_allocate_vmo(void* ctx, uint64_t size, zx_handle_t* vmo_out);


    // Internal routines
    template <typename RequestType, typename ResponseType>
    void send_command_response(const RequestType* cmd, ResponseType** res);

    zx_status_t get_display_info();
    zx_status_t allocate_2d_resource(uint32_t* resource_id, uint32_t width, uint32_t height);
    zx_status_t attach_backing(uint32_t resource_id, zx_paddr_t ptr, size_t buf_len);
    zx_status_t set_scanout(uint32_t scanout_id, uint32_t resource_id, uint32_t width, uint32_t height);
    zx_status_t flush_resource(uint32_t resource_id, uint32_t width, uint32_t height);
    zx_status_t transfer_to_host_2d(uint32_t resource_id, uint32_t width, uint32_t height);

    zx_status_t virtio_gpu_start();
    thrd_t start_thread_ = {};

    // the main virtio ring
    Ring vring_ = {this};

    // display protocol ops
    display_controller_protocol_ops_t display_proto_ops_ = {};

    // gpu op
    io_buffer_t gpu_req_;

    // A saved copy of the display
    virtio_gpu_resp_display_info::virtio_gpu_display_one pmode_ = {};
    int pmode_id_ = -1;

    uint32_t next_resource_id_ = 1;

    fbl::Mutex request_lock_;
    sem_t request_sem_;
    sem_t response_sem_;

    // Flush thread
    void virtio_gpu_flusher();
    thrd_t flush_thread_ = {};
    fbl::Mutex flush_lock_;
    cnd_t flush_cond_ = {};
    bool flush_pending_ = false;

    display_controller_cb_t* dc_cb_;
    void* dc_cb_ctx_;

    struct imported_image* current_fb_;
    struct imported_image* displayed_fb_;

    zx_pixel_format_t supported_formats_ = ZX_PIXEL_FORMAT_RGB_x888;
};

} // namespace virtio
