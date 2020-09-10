// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AMLOGIC_DISPLAY_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AMLOGIC_DISPLAY_H_

#include <lib/device-protocol/display-panel.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/bti.h>
#include <lib/zx/interrupt.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/pixelformat.h>
#include <zircon/types.h>

#include <cstdint>
#include <memory>

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/protocol/amlogiccanvas.h>
#include <ddk/protocol/display/clamprgb.h>
#include <ddk/protocol/dsiimpl.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/sysmem.h>
#include <ddktl/device.h>
#include <ddktl/protocol/display/capture.h>
#include <ddktl/protocol/display/clamprgb.h>
#include <ddktl/protocol/display/controller.h>
#include <ddktl/protocol/dsiimpl.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>

#include "aml-dsi-host.h"
#include "amlogic-clock.h"
#include "common.h"
#include "osd.h"
#include "vpu.h"
#include "zircon/errors.h"

namespace amlogic_display {

struct ImageInfo : public fbl::DoublyLinkedListable<std::unique_ptr<ImageInfo>> {
  ~ImageInfo() {
    if (canvas.ctx && canvas_idx > 0) {
      amlogic_canvas_free(&canvas, canvas_idx);
    }
    if (pmt) {
      pmt.unpin();
    }
  }
  amlogic_canvas_protocol_t canvas;
  uint8_t canvas_idx;
  uint32_t image_height;
  uint32_t image_width;

  bool is_afbc;
  zx::pmt pmt;
  zx_paddr_t paddr;
};

class AmlogicDisplay;
class ClampRgb;

// AmlogicDisplay will implement only a few subset of Device.
using DeviceType = ddk::Device<AmlogicDisplay, ddk::GetProtocolable, ddk::Unbindable,
                               ddk::Suspendable, ddk::Resumable>;
class AmlogicDisplay
    : public DeviceType,
      public ddk::DisplayControllerImplProtocol<AmlogicDisplay, ddk::base_protocol>,
      public ddk::DisplayCaptureImplProtocol<AmlogicDisplay>,
      public ddk::DisplayClampRgbImplProtocol<AmlogicDisplay> {
 public:
  AmlogicDisplay(zx_device_t* parent) : DeviceType(parent) {}

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

  void DisplayCaptureImplSetDisplayCaptureInterface(
      const display_capture_interface_protocol_t* intf);

  zx_status_t DisplayCaptureImplImportImageForCapture(zx_unowned_handle_t collection,
                                                      uint32_t index, uint64_t* out_capture_handle);
  zx_status_t DisplayCaptureImplStartCapture(uint64_t capture_handle);
  zx_status_t DisplayCaptureImplReleaseCapture(uint64_t capture_handle);
  bool DisplayCaptureImplIsCaptureCompleted() __TA_EXCLUDES(capture_lock_);

  zx_status_t DisplayClampRgbImplSetMinimumRgb(uint8_t minimum_rgb);

  // Required functions for DeviceType
  void DdkSuspend(ddk::SuspendTxn txn);
  void DdkResume(ddk::ResumeTxn txn);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out_protocol);

  void Dump();

 private:
  enum {
    FRAGMENT_PDEV,
    FRAGMENT_DSI,
    FRAGMENT_LCD_GPIO,
    FRAGMENT_SYSMEM,
    FRAGMENT_CANVAS,
    FRAGMENT_COUNT,
  };
  zx_device_t* fragments_[FRAGMENT_COUNT];

  zx_status_t SetupDisplayInterface();
  int VSyncThread();
  int CaptureThread();
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
  thrd_t capture_thread_;

  // Protocol handles used in by this driver
  pdev_protocol_t pdev_ = {};
  amlogic_canvas_protocol_t canvas_ = {};
  sysmem_protocol_t sysmem_ = {};

  // Board Info
  pdev_board_info_t board_info_;

  // Interrupts
  zx::interrupt vsync_irq_;
  zx::interrupt vd1_wr_irq_;

  // Locks used by the display driver
  fbl::Mutex display_lock_;  // general display state (i.e. display_id)
  fbl::Mutex image_lock_;    // used for accessing imported_images_
  fbl::Mutex capture_lock_;  // general capture state

  // TODO(stevensd): This can race if this is changed right after
  // vsync but before the interrupt is handled.
  uint64_t current_image_ TA_GUARDED(display_lock_) = 0;
  bool current_image_valid_ TA_GUARDED(display_lock_) = false;

  // display dimensions and format
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  zx_pixel_format_t format_ = 0;

  const display_setting_t* init_disp_table_ = nullptr;

  bool full_init_done_ = false;

  // board revision and panel type detected by the display driver
  uint32_t panel_type_ TA_GUARDED(display_lock_) = PANEL_UNKNOWN;

  // Display structure used by various layers of display controller
  display_setting_t disp_setting_;

  // Display controller related data
  ddk::DisplayControllerInterfaceProtocolClient dc_intf_ TA_GUARDED(display_lock_);

  // Display Capture interface protocol
  ddk::DisplayCaptureInterfaceProtocolClient capture_intf_ TA_GUARDED(capture_lock_);

  // The ID for currently active capture
  uint64_t capture_active_id_ TA_GUARDED(capture_lock_);

  // Imported Images
  fbl::DoublyLinkedList<std::unique_ptr<ImageInfo>> imported_images_ TA_GUARDED(image_lock_);
  fbl::DoublyLinkedList<std::unique_ptr<ImageInfo>> imported_captures_ TA_GUARDED(capture_lock_);

  // DSIIMPL Protocol
  ddk::DsiImplProtocolClient dsiimpl_ = {};

  // Objects
  std::unique_ptr<amlogic_display::Vpu> vpu_;
  std::unique_ptr<amlogic_display::Osd> osd_;
  std::unique_ptr<amlogic_display::AmlogicDisplayClock> clock_;
  std::unique_ptr<amlogic_display::AmlDsiHost> dsi_host_;

  // Monitoring
  inspect::Inspector inspector_;
};

}  // namespace amlogic_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AMLOGIC_DISPLAY_H_
