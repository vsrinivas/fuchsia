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
#include <lib/mmio/mmio.h>
#include <ddktl/protocol/display/controller.h>

#include <ddk/protocol/platform-device-lib.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/sysmem.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <zircon/listnode.h>

#include "ovl.h"
#include "disp-rdma.h"

namespace mt8167s_display {

class Mt8167sDisplay;

// Mt8167sDisplay will implement only a few subset of Device
using DeviceType = ddk::Device<Mt8167sDisplay, ddk::Unbindable>;

class Mt8167sDisplay : public DeviceType,
                       public ddk::DisplayControllerImplProtocol<Mt8167sDisplay,
                                                                 ddk::base_protocol> {
public:
    Mt8167sDisplay(zx_device_t* parent, uint32_t width, uint32_t height)
        : DeviceType(parent), width_(width), height_(height) {}

    // This function is called from the c-bind function upon driver matching
    zx_status_t Bind();

    // Required functions needed to implement Display Controller Protocol
    void DisplayControllerImplSetDisplayControllerInterface(const display_controller_interface_t* intf);
    zx_status_t DisplayControllerImplImportVmoImage(image_t* image, zx::vmo vmo, size_t offset);
    void DisplayControllerImplReleaseImage(image_t* image);
    uint32_t DisplayControllerImplCheckConfiguration(const display_config_t** display_config,
                                                     size_t display_count,
                                                     uint32_t** layer_cfg_results,
                                                     size_t* layer_cfg_result_count);
    void DisplayControllerImplApplyConfiguration(const display_config_t** display_config,
                                                 size_t display_count);
    uint32_t DisplayControllerImplComputeLinearStride(uint32_t width, zx_pixel_format_t format);
    zx_status_t DisplayControllerImplAllocateVmo(uint64_t size, zx::vmo* vmo_out);
    zx_status_t DisplayControllerImplGetSysmemConnection(zx::channel connection);
    zx_status_t DisplayControllerImplSetBufferCollectionConstraints(const image_t* config,
                                                                    uint32_t collection);

    int VSyncThread();

    // Required functions for DeviceType
    void DdkUnbind();
    void DdkRelease();

private:
    void PopulateAddedDisplayArgs(added_display_args_t* args);

    void Shutdown();

    // This function initializes the various components within the display subsytem such as
    // Overlay Engine, RDMA Engine, DSI, HDMI, etc
    zx_status_t DisplaySubsystemInit();

    // Zircon handles
    zx::bti bti_;

    // Thread handles
    thrd_t vsync_thread_;

    // Protocol handles
    pdev_protocol_t pdev_ = {};
    sysmem_protocol_t sysmem_ = {};

    // Interrupts
    zx::interrupt vsync_irq_;

    // Locks used by the display driver
    fbl::Mutex display_lock_; // general display state (i.e. display_id)
    fbl::Mutex image_lock_;   // used for accessing imported_images_

    // display dimensions and format
    const uint32_t width_;
    const uint32_t height_;

    // Imported Images
    list_node_t imported_images_ TA_GUARDED(image_lock_);

    // Display controller related data
    ddk::DisplayControllerInterfaceClient dc_intf_ TA_GUARDED(display_lock_);

    // Objects
    fbl::unique_ptr<mt8167s_display::Ovl>                   ovl_;
    fbl::unique_ptr<mt8167s_display::DispRdma>              disp_rdma_;
};

} // namespace mt8167s_display
