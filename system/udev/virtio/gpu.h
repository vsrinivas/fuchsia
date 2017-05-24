// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include "device.h"
#include "ring.h"
#include "virtio_gpu.h"

#include <magenta/compiler.h>
#include <stdlib.h>

#include <ddk/protocol/display.h>

namespace virtio {

class Ring;

class GpuDevice : public Device {
public:
    GpuDevice(mx_device_t* device);
    virtual ~GpuDevice();

    virtual mx_status_t Init();

    virtual void IrqRingUpdate();
    virtual void IrqConfigChange();

    void* framebuffer() const { return fb_; }
    const virtio_gpu_resp_display_info::virtio_gpu_display_one* pmode() const { return &pmode_; }

    void Flush();

private:
    // DDK driver hooks
    static mx_status_t virtio_gpu_set_mode(mx_device_t* dev, mx_display_info_t* info);
    static mx_status_t virtio_gpu_get_mode(mx_device_t* dev, mx_display_info_t* info);
    static mx_status_t virtio_gpu_get_framebuffer(mx_device_t* dev, void** framebuffer);
    static void virtio_gpu_flush(mx_device_t* dev);

    // internal routines
    mx_status_t send_command_response(const void* cmd, size_t cmd_len, void** _res, size_t res_len);
    mx_status_t get_display_info();
    mx_status_t allocate_2d_resource(uint32_t* resource_id, uint32_t width, uint32_t height);
    mx_status_t attach_backing(uint32_t resource_id, mx_paddr_t ptr, size_t buf_len);
    mx_status_t set_scanout(uint32_t scanout_id, uint32_t resource_id, uint32_t width, uint32_t height);
    mx_status_t flush_resource(uint32_t resource_id, uint32_t width, uint32_t height);
    mx_status_t transfer_to_host_2d(uint32_t resource_id, uint32_t width, uint32_t height);

    mx_status_t virtio_gpu_start();
    static int virtio_gpu_start_entry(void* arg);
    thrd_t start_thread_ = {};

    // the main virtio ring
    Ring vring_ = {this};

    // display protocol ops
    mx_display_protocol_t display_proto_ops_ = {};

    // gpu op
    void* gpu_req_ = nullptr;
    mx_paddr_t gpu_req_pa_ = 0;

    // a saved copy of the display
    virtio_gpu_resp_display_info::virtio_gpu_display_one pmode_ = {};
    int pmode_id_ = -1;

    // resource id that is set as scanout
    uint32_t display_resource_id_ = 0;

    // next resource id
    uint32_t next_resource_id_ = -1;

    // framebuffer
    void* fb_ = nullptr;
    mx_paddr_t fb_pa_ = 0;

    // request condition
    mxtl::Mutex request_lock_;
    cnd_t request_cond_ = {};

    // flush thread
    void virtio_gpu_flusher();
    static int virtio_gpu_flusher_entry(void* arg);
    thrd_t flush_thread_ = {};
    mxtl::Mutex flush_lock_;
    cnd_t flush_cond_ = {};
    bool flush_pending_ = false;
};

} // namespace virtio
