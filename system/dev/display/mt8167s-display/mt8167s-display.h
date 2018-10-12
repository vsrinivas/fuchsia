// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/bti.h>
#include <lib/zx/interrupt.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/thread_annotations.h>

#include <ddk/debug.h>
#include <ddktl/device.h>
#include <ddktl/mmio.h>
#include <ddktl/protocol/display-controller.h>

#include <fbl/atomic.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <zircon/listnode.h>
#include <ddk/protocol/platform-device.h>

namespace mt8167s_display {

class Mt8167sDisplay;

// Mt8167sDisplay will implement only a few subset of Device
using DeviceType = ddk::Device<Mt8167sDisplay, ddk::Unbindable>;

class Mt8167sDisplay : public DeviceType,
                       public ddk::DisplayControllerProtocol<Mt8167sDisplay> {
public:
    Mt8167sDisplay(zx_device_t* parent, uint32_t width, uint32_t height)
        : DeviceType(parent), width_(width), height_(height) {}

    // This function is called from the c-bind function upon driver matching
    zx_status_t Bind();

    // Required functions needed to implement Display Controller Protocol
    void SetDisplayControllerCb(void* cb_ctx, display_controller_cb_t* cb);
    zx_status_t ImportVmoImage(image_t* image, const zx::vmo& vmo, size_t offset);
    void ReleaseImage(image_t* image);
    void CheckConfiguration(const display_config_t** display_config,
                            uint32_t* display_cfg_result, uint32_t** layer_cfg_result,
                            uint32_t display_count);
    void ApplyConfiguration(const display_config_t** display_config, uint32_t display_count);
    uint32_t ComputeLinearStride(uint32_t width, zx_pixel_format_t format);
    zx_status_t AllocateVmo(uint64_t size, zx_handle_t* vmo_out);

    int VSyncThread();

    // Required functions for DeviceType
    void DdkUnbind();
    void DdkRelease();

private:
    void PopulateAddedDisplayArgs(added_display_args_t* args);

    void Shutdown();

    // Zircon handles
    zx::bti bti_;

    // Thread handles
    thrd_t vsync_thread_;

    // Protocol handles
    platform_device_protocol_t pdev_ = {};

    // Interrupts
    zx::interrupt vsync_irq_;

    // Locks used by the display driver
    fbl::Mutex display_lock_; // general display state (i.e. display_id)
    fbl::Mutex image_lock_;   // used for accessing imported_images_

    uint8_t current_image_ TA_GUARDED(display_lock_);
    bool current_image_valid_ TA_GUARDED(display_lock_);

    fbl::unique_ptr<ddk::MmioBuffer> ovl_mmio_;

    // display dimensions and format
    const uint32_t width_;
    const uint32_t height_;

    // Imported Images
    list_node_t imported_images_ TA_GUARDED(image_lock_);

    // Display controller related data
    display_controller_cb_t* dc_cb_ TA_GUARDED(display_lock_);
    void* dc_cb_ctx_ TA_GUARDED(display_lock_);
};

} // namespace mt8167s_display