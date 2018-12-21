// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unistd.h>

#include <zircon/compiler.h>
#include <zircon/pixelformat.h>
#include <zircon/thread_annotations.h>

#include <ddk/debug.h>
#include <ddk/driver.h>

#include <ddktl/device.h>
#include <ddktl/protocol/display/controller.h>

#include <fbl/atomic.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>

namespace dummy_display {

class DummyDisplay;

using DeviceType = ddk::Device<DummyDisplay, ddk::Unbindable>;

class DummyDisplay : public DeviceType,
                     public ddk::DisplayControllerImplProtocol<DummyDisplay, ddk::base_protocol> {
public:
    DummyDisplay(zx_device_t* parent)
        : DeviceType(parent) {}

    // This function is called from the c-bind function upon driver matching
    zx_status_t Bind();

    // Required functions needed to implement Display Controller Protocol
    void DisplayControllerImplSetDisplayControllerInterface(
        const display_controller_interface_t* intf);
    zx_status_t DisplayControllerImplImportVmoImage(image_t* image, zx::vmo vmo, size_t offset);
    void DisplayControllerImplReleaseImage(image_t* image);
    uint32_t DisplayControllerImplCheckConfiguration(const display_config_t** display_configs,
                                                     size_t display_count,
                                                     uint32_t** layer_cfg_results,
                                                     size_t* layer_cfg_result_count);
    void DisplayControllerImplApplyConfiguration(const display_config_t** display_config,
                                                 size_t display_count);
    uint32_t DisplayControllerImplComputeLinearStride(uint32_t width, zx_pixel_format_t format);
    zx_status_t DisplayControllerImplAllocateVmo(uint64_t size, zx::vmo* vmo_out);

    // Required functions for DeviceType
    void DdkUnbind();
    void DdkRelease();

private:
    zx_status_t SetupDisplayInterface();
    int VSyncThread();
    void PopulateAddedDisplayArgs(added_display_args_t* args);

    fbl::atomic_bool vsync_shutdown_flag_ = false;

    // Thread handles
    thrd_t vsync_thread_;

    // Locks used by the display driver
    fbl::Mutex display_lock_; // general display state (i.e. display_id)

    uint64_t current_image_ TA_GUARDED(display_lock_);
    bool current_image_valid_ TA_GUARDED(display_lock_);

    // Display controller related data
    ddk::DisplayControllerInterfaceClient dc_intf_ TA_GUARDED(display_lock_);
};

} // namespace dummy_display
