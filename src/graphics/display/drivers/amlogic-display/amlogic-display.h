// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AMLOGIC_DISPLAY_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AMLOGIC_DISPLAY_H_

#include <fuchsia/hardware/amlogiccanvas/cpp/banjo.h>
#include <fuchsia/hardware/display/capture/cpp/banjo.h>
#include <fuchsia/hardware/display/clamprgb/cpp/banjo.h>
#include <fuchsia/hardware/display/controller/cpp/banjo.h>
#include <fuchsia/hardware/i2cimpl/cpp/banjo.h>
#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <fuchsia/hardware/sysmem/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
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

#include <ddktl/device.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>

#include "src/graphics/display/drivers/amlogic-display/common.h"
#include "src/graphics/display/drivers/amlogic-display/osd.h"
#include "src/graphics/display/drivers/amlogic-display/vout.h"
#include "src/graphics/display/drivers/amlogic-display/vpu.h"
#include "zircon/errors.h"

namespace amlogic_display {

struct ImageInfo : public fbl::DoublyLinkedListable<std::unique_ptr<ImageInfo>> {
  ~ImageInfo() {
    zxlogf(INFO, "Destroying image on canvas %d", canvas_idx);
    if (canvas.is_valid()) {
      canvas.Free(canvas_idx);
    }
    if (pmt) {
      pmt.unpin();
    }
  }
  ddk::AmlogicCanvasProtocolClient canvas;
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
using DeviceType = ddk::Device<AmlogicDisplay, ddk::GetProtocolable, ddk::Suspendable,
                               ddk::Resumable, ddk::ChildPreReleaseable>;
class AmlogicDisplay
    : public DeviceType,
      public ddk::DisplayControllerImplProtocol<AmlogicDisplay, ddk::base_protocol>,
      public ddk::DisplayCaptureImplProtocol<AmlogicDisplay>,
      public ddk::DisplayClampRgbImplProtocol<AmlogicDisplay>,
      public ddk::I2cImplProtocol<AmlogicDisplay> {
 public:
  AmlogicDisplay(zx_device_t* parent) : DeviceType(parent) {}

  // This function is called from the c-bind function upon driver matching
  zx_status_t Bind();

  // Required functions needed to implement Display Controller Protocol
  void DisplayControllerImplSetDisplayControllerInterface(
      const display_controller_interface_protocol_t* intf);
  zx_status_t DisplayControllerImplImportImage(image_t* image, zx_unowned_handle_t handle,
                                               uint32_t index);
  void DisplayControllerImplReleaseImage(image_t* image);
  uint32_t DisplayControllerImplCheckConfiguration(const display_config_t** display_configs,
                                                   size_t display_count,
                                                   uint32_t** layer_cfg_results,
                                                   size_t* layer_cfg_result_count);
  void DisplayControllerImplApplyConfiguration(const display_config_t** display_config,
                                               size_t display_count,
                                               const config_stamp_t* config_stamp);
  void DisplayControllerImplSetEld(uint64_t display_id, const uint8_t* raw_eld_list,
                                   size_t raw_eld_count) {}  // No ELD required for non-HDA systems.
  zx_status_t DisplayControllerImplGetSysmemConnection(zx::channel connection);
  zx_status_t DisplayControllerImplSetBufferCollectionConstraints(const image_t* config,
                                                                  uint32_t collection);
  zx_status_t DisplayControllerImplGetSingleBufferFramebuffer(zx::vmo* out_vmo,
                                                              uint32_t* out_stride) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t DisplayControllerImplSetDisplayPower(uint64_t display_id, bool power_on);

  void DisplayCaptureImplSetDisplayCaptureInterface(
      const display_capture_interface_protocol_t* intf);

  zx_status_t DisplayCaptureImplImportImageForCapture(zx_unowned_handle_t collection,
                                                      uint32_t index, uint64_t* out_capture_handle);
  zx_status_t DisplayCaptureImplStartCapture(uint64_t capture_handle);
  zx_status_t DisplayCaptureImplReleaseCapture(uint64_t capture_handle);
  bool DisplayCaptureImplIsCaptureCompleted() __TA_EXCLUDES(capture_lock_);

  zx_status_t DisplayClampRgbImplSetMinimumRgb(uint8_t minimum_rgb);

  // Required functions for I2cImpl
  uint32_t I2cImplGetBusBase() { return 0; }
  uint32_t I2cImplGetBusCount() { return 1; }
  zx_status_t I2cImplGetMaxTransferSize(uint32_t bus_id, size_t* out_size) {
    *out_size = UINT32_MAX;
    return ZX_OK;
  }
  zx_status_t I2cImplSetBitrate(uint32_t bus_id, uint32_t bitrate) { return ZX_OK; }  // no-op
  zx_status_t I2cImplTransact(uint32_t bus_id, const i2c_impl_op_t* op_list, size_t op_count) {
    return vout_->EdidTransfer(bus_id, op_list, op_count);
  }

  // Required functions for DeviceType
  void DdkSuspend(ddk::SuspendTxn txn);
  void DdkResume(ddk::ResumeTxn txn);
  void DdkRelease();
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out_protocol);
  void DdkChildPreRelease(void* child_ctx) {
    fbl::AutoLock lock(&display_lock_);
    dc_intf_ = ddk::DisplayControllerInterfaceProtocolClient();
  }

  void Dump() { vout_->Dump(); }

  void SetFormatSupportCheck(fit::function<bool(zx_pixel_format_t)> fn) {
    format_support_check_ = std::move(fn);
  }

  void SetCanvasForTesting(ddk::AmlogicCanvasProtocolClient canvas) { canvas_ = canvas; }

 private:
  zx_status_t SetupDisplayInterface();
  int VSyncThread();
  int CaptureThread();
  int HpdThread();
  void PopulatePanelType() TA_REQ(display_lock_);

  // This function enables the display hardware. This function is disruptive and causes
  // unexpected pixels to be visible on the screen.
  zx_status_t DisplayInit() TA_REQ(display_lock_);

  // Power cycle the device and bring up clocks. Only needed when resuming the
  // driver, as the bootloader will initialize the display when the machine is
  // powered on.
  zx_status_t RestartDisplay() TA_REQ(display_lock_);

  bool fully_initialized() const { return full_init_done_.load(std::memory_order_relaxed); }
  void set_fully_initialized() { full_init_done_.store(true, std::memory_order_release); }

  // Zircon handles
  zx::bti bti_;
  zx::interrupt inth_;

  // Thread handles
  thrd_t vsync_thread_;
  thrd_t capture_thread_;

  // Protocol handles used in by this driver
  ddk::PDev pdev_;
  ddk::AmlogicCanvasProtocolClient canvas_;
  ddk::SysmemProtocolClient sysmem_;

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

  // Relaxed is safe because full_init_done_ only ever moves from false to true.
  std::atomic<bool> full_init_done_ = false;

  // Display controller related data
  ddk::DisplayControllerInterfaceProtocolClient dc_intf_ TA_GUARDED(display_lock_);

  // Display Capture interface protocol
  ddk::DisplayCaptureInterfaceProtocolClient capture_intf_ TA_GUARDED(capture_lock_);

  // The ID for currently active capture
  uint64_t capture_active_id_ TA_GUARDED(capture_lock_);

  // Imported Images
  fbl::DoublyLinkedList<std::unique_ptr<ImageInfo>> imported_images_ TA_GUARDED(image_lock_);
  fbl::DoublyLinkedList<std::unique_ptr<ImageInfo>> imported_captures_ TA_GUARDED(capture_lock_);

  // Objects: only valid if fully_initialized()
  std::unique_ptr<amlogic_display::Vpu> vpu_;
  std::unique_ptr<amlogic_display::Osd> osd_;
  std::unique_ptr<amlogic_display::Vout> vout_;

  // Monitoring. We create a named "amlogic-display" node to allow for easier filtering
  // of inspect tree when defining selectors and metrics.
  inspect::Inspector inspector_;
  inspect::Node root_node_;

  uint64_t display_id_ = PANEL_DISPLAY_ID;
  bool display_attached_ = false;

  // Hot Plug Detection
  ddk::GpioProtocolClient hpd_gpio_{};
  zx::interrupt hpd_irq_;
  thrd_t hpd_thread_;

  fit::function<bool(zx_pixel_format_t format)> format_support_check_ = nullptr;
};

}  // namespace amlogic_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AMLOGIC_DISPLAY_H_
