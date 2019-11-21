// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_ASTRO_DISPLAY_ASTRO_DISPLAY_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_ASTRO_DISPLAY_ASTRO_DISPLAY_H_

#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/bti.h>
#include <lib/zx/interrupt.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/pixelformat.h>

#include <memory>

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/protocol/amlogiccanvas.h>
#include <ddk/protocol/dsiimpl.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/sysmem.h>
#include <ddktl/device.h>
#include <ddktl/protocol/display/controller.h>
#include <ddktl/protocol/dsiimpl.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>

#include "aml-dsi-host.h"
#include "astro-clock.h"
#include "common.h"
#include "osd.h"
#include "vpu.h"
#include "zircon/errors.h"

namespace astro_display {

struct ImageInfo : public fbl::DoublyLinkedListable<std::unique_ptr<ImageInfo>> {
  uint8_t canvas_idx;
};

class AstroDisplay;

// AstroDisplay will implement only a few subset of Device.
using DeviceType = ddk::Device<AstroDisplay, ddk::UnbindableNew>;

class AstroDisplay : public DeviceType,
                     public ddk::DisplayControllerImplProtocol<AstroDisplay, ddk::base_protocol> {
 public:
  AstroDisplay(zx_device_t* parent) : DeviceType(parent) {}

  // This function is called from the c-bind function upon driver matching
  zx_status_t Bind();

  // Required functions needed to implement Display Controller Protocol
  void DisplayControllerImplSetDisplayControllerInterface(
      const display_controller_interface_protocol_t* intf);
  zx_status_t DisplayControllerImplImportVmoImage(image_t* image, zx::vmo vmo, size_t offset);
  zx_status_t DisplayControllerImplImportImage(image_t* image, zx_unowned_handle_t handle,
                                               uint32_t index);
  void DisplayControllerImplReleaseImage(image_t* image);
  uint32_t DisplayControllerImplCheckConfiguration(const display_config_t** display_configs,
                                                   size_t display_count,
                                                   uint32_t** layer_cfg_results,
                                                   size_t* layer_cfg_result_count);
  void DisplayControllerImplApplyConfiguration(const display_config_t** display_config,
                                               size_t display_count);
  zx_status_t DisplayControllerImplGetSysmemConnection(zx::channel connection);
  zx_status_t DisplayControllerImplSetBufferCollectionConstraints(const image_t* config,
                                                                  uint32_t collection);
  zx_status_t DisplayControllerImplGetSingleBufferFramebuffer(zx::vmo* out_vmo,
                                                              uint32_t* out_stride) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Required functions for DeviceType
  void DdkUnbindNew(ddk::UnbindTxn txn);
  void DdkRelease();

  void Dump();

 private:
  enum {
    COMPONENT_PDEV,
    COMPONENT_DSI,
    COMPONENT_PANEL_GPIO,
    COMPONENT_LCD_GPIO,
    COMPONENT_SYSMEM,
    COMPONENT_CANVAS,
    COMPONENT_COUNT,
  };
  zx_device_t* components_[COMPONENT_COUNT];

  zx_status_t SetupDisplayInterface();
  int VSyncThread();
  void CopyDisplaySettings();
  void PopulateAddedDisplayArgs(added_display_args_t* args);
  void PopulatePanelType() TA_REQ(display_lock_);

  // This function enables the display hardware. This function is disruptive and causes
  // unexpected pixels to be visible on the screen.
  zx_status_t DisplayInit() TA_REQ(display_lock_);

  // Zircon handles
  zx::bti bti_;
  zx::interrupt inth_;

  // Thread handles
  thrd_t vsync_thread_;

  // Protocol handles used in by this driver
  pdev_protocol_t pdev_ = {};
  gpio_protocol_t gpio_ = {};
  amlogic_canvas_protocol_t canvas_ = {};
  sysmem_protocol_t sysmem_ = {};

  // Board Info
  pdev_board_info_t board_info_;

  // Interrupts
  zx::interrupt vsync_irq_;

  // Locks used by the display driver
  fbl::Mutex display_lock_;  // general display state (i.e. display_id)
  fbl::Mutex image_lock_;    // used for accessing imported_images_

  // TODO(stevensd): This can race if this is changed right after
  // vsync but before the interrupt is handled.
  uint64_t current_image_ TA_GUARDED(display_lock_);
  bool current_image_valid_ TA_GUARDED(display_lock_);

  // display dimensions and format
  uint32_t width_;
  uint32_t height_;
  uint32_t stride_;
  zx_pixel_format_t format_;

  const display_setting_t* init_disp_table_ = nullptr;

  // This flag is used to skip all driver initializations for older
  // boards that we don't support. Those boards will depend on U-Boot
  // to set things up
  bool skip_disp_init_ TA_GUARDED(display_lock_);

  bool full_init_done_ = false;

  // board revision and panel type detected by the display driver
  uint8_t panel_type_ TA_GUARDED(display_lock_);

  // Display structure used by various layers of display controller
  display_setting_t disp_setting_;

  // Display controller related data
  ddk::DisplayControllerInterfaceProtocolClient dc_intf_ TA_GUARDED(display_lock_);

  // Imported Images
  fbl::DoublyLinkedList<std::unique_ptr<ImageInfo>> imported_images_ TA_GUARDED(image_lock_);

  // DSIIMPL Protocol
  ddk::DsiImplProtocolClient dsiimpl_ = {};

  // Objects
  std::unique_ptr<astro_display::Vpu> vpu_;
  std::unique_ptr<astro_display::Osd> osd_;
  std::unique_ptr<astro_display::AstroDisplayClock> clock_;
  std::unique_ptr<astro_display::AmlDsiHost> dsi_host_;
};

}  // namespace astro_display

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_ASTRO_DISPLAY_ASTRO_DISPLAY_H_
