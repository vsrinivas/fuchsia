// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unistd.h>

#include <zircon/compiler.h>
#include <zircon/pixelformat.h>
#include <zircon/thread_annotations.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/bti.h>

#include <ddk/driver.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/amlogiccanvas.h>
#include <ddk/debug.h>

#include <ddktl/protocol/display/controller.h>
#include <ddktl/device.h>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>

#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include "vpu.h"
#include "osd.h"
#include "astro-clock.h"
#include "aml-dsi-host.h"
#include "common.h"

namespace astro_display {

class AstroDisplay;

constexpr uint8_t kMaxImportedImages = 255;
using ImportedImageBitmap = bitmap::RawBitmapGeneric<bitmap::FixedStorage<kMaxImportedImages>>;

// AstroDisplay will implement only a few subset of Device.
using DeviceType = ddk::Device<AstroDisplay, ddk::Unbindable>;

class AstroDisplay : public DeviceType,
                     public ddk::DisplayControllerImplProtocol<AstroDisplay> {
public:
    AstroDisplay(zx_device_t* parent, uint32_t width, uint32_t height)
        : DeviceType(parent), width_(width), height_(height) {}

    // This function is called from the c-bind function upon driver matching
    zx_status_t Bind();

    // Required functions needed to implement Display Controller Protocol
    void DisplayControllerImplSetDisplayControllerInterface(
            const display_controller_interface_t* intf);
    zx_status_t DisplayControllerImplImportVmoImage(image_t* image, zx_handle_t vmo, size_t offset);
    void DisplayControllerImplReleaseImage(image_t* image);
    uint32_t DisplayControllerImplCheckConfiguration(const display_config_t** display_configs,
                                                     size_t display_count,
                                                     uint32_t** layer_cfg_results,
                                                     size_t* layer_cfg_result_count);
    void DisplayControllerImplApplyConfiguration(const display_config_t** display_config,
                                             size_t display_count);
    uint32_t DisplayControllerImplComputeLinearStride(uint32_t width, zx_pixel_format_t format);
    zx_status_t DisplayControllerImplAllocateVmo(uint64_t size, zx_handle_t* vmo_out);

    // Required functions for DeviceType
    void DdkUnbind();
    void DdkRelease();

    void Dump();

private:
    zx_status_t SetupDisplayInterface();
    int VSyncThread();
    void CopyDisplaySettings();
    void PopulateAddedDisplayArgs(added_display_args_t* args);
    void PopulatePanelType() TA_REQ(display_lock_);

    // Zircon handles
    zx::bti                             bti_;
    zx::interrupt                       inth_;

    // Thread handles
    thrd_t                              vsync_thread_;

    // Protocol handles used in by this driver
    pdev_protocol_t          pdev_ = {};
    gpio_protocol_t                     gpio_ = {};
    canvas_protocol_t                   canvas_ = {};

    // Board Info
    pdev_board_info_t                   board_info_;

    // Interrupts
    zx::interrupt                       vsync_irq_;

    // Locks used by the display driver
    fbl::Mutex                          display_lock_; // general display state (i.e. display_id)
    fbl::Mutex                          image_lock_; // used for accessing imported_images_

    // TODO(stevensd): This can race if this is changed right after
    // vsync but before the interrupt is handled.
    uint8_t                             current_image_ TA_GUARDED(display_lock_);;
    bool                                current_image_valid_ TA_GUARDED(display_lock_);;

    // display dimensions and format
    const uint32_t                      width_;
    const uint32_t                      height_;
    uint32_t                            stride_;
    zx_pixel_format_t                   format_;

    const DisplaySetting*               init_disp_table_ = nullptr;

    // This flag is used to skip all driver initializations for older
    // boards that we don't support. Those boards will depend on U-Boot
    // to set things up
    bool                                skip_disp_init_ TA_GUARDED(display_lock_);

    // board revision and panel type detected by the display driver
    uint8_t                             panel_type_ TA_GUARDED(display_lock_);

    // Display structure used by various layers of display controller
    DisplaySetting                      disp_setting_;

    // Display controller related data
    ddk::DisplayControllerInterfaceProxy dc_intf_ TA_GUARDED(display_lock_);

    // Simple hashtable
    ImportedImageBitmap                  imported_images_ TA_GUARDED(image_lock_);;

    // Objects
    fbl::unique_ptr<astro_display::Vpu>                 vpu_;
    fbl::unique_ptr<astro_display::Osd>                 osd_;
    fbl::unique_ptr<astro_display::AstroDisplayClock>   clock_;
    fbl::unique_ptr<astro_display::AmlDsiHost>          dsi_host_;
};

} // namespace astro_display
