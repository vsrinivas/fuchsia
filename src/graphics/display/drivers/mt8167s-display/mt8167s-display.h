// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_MT8167S_DISPLAY_MT8167S_DISPLAY_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_MT8167S_DISPLAY_MT8167S_DISPLAY_H_

#include <lib/device-protocol/display-panel.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/bti.h>
#include <lib/zx/interrupt.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/listnode.h>

#include <array>
#include <memory>

#include <ddk/debug.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/sysmem.h>
#include <ddktl/device.h>
#include <ddktl/protocol/display/controller.h>
#include <ddktl/protocol/dsiimpl.h>
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/power.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>

#include "aal.h"
#include "ccorr.h"
#include "color.h"
#include "disp-rdma.h"
#include "dither.h"
#include "gamma.h"
#include "mt-dsi-host.h"
#include "mt-sysconfig.h"
#include "ovl.h"

namespace mt8167s_display {

struct ImageInfo : public fbl::DoublyLinkedListable<std::unique_ptr<ImageInfo>> {
  ~ImageInfo() {
    if (pmt)
      pmt.unpin();
  }

  zx::pmt pmt;
  zx_paddr_t paddr;
  uint32_t pitch;
};

class Mt8167sDisplay;

// Mt8167sDisplay will implement only a few subset of Device
using DeviceType = ddk::Device<Mt8167sDisplay, ddk::Unbindable>;

class Mt8167sDisplay
    : public DeviceType,
      public ddk::DisplayControllerImplProtocol<Mt8167sDisplay, ddk::base_protocol> {
 public:
  Mt8167sDisplay(zx_device_t* parent) : DeviceType(parent) {}

  // This function is called from the c-bind function upon driver matching
  zx_status_t Bind();

  // Required functions needed to implement Display Controller Protocol
  void DisplayControllerImplSetDisplayControllerInterface(
      const display_controller_interface_protocol_t* intf);
  zx_status_t DisplayControllerImplImportVmoImage(image_t* image, zx::vmo vmo, size_t offset);
  zx_status_t DisplayControllerImplImportImage(image_t* image, zx_unowned_handle_t handle,
                                               uint32_t index);
  void DisplayControllerImplReleaseImage(image_t* image);
  uint32_t DisplayControllerImplCheckConfiguration(const display_config_t** display_config,
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
  int VSyncThread();

  // Required functions for DeviceType
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  void SetBtiForTesting(zx::bti bti) { bti_ = std::move(bti); }

 private:
  void PopulateAddedDisplayArgs(added_display_args_t* args);
  void CopyDisplaySettings();
  void Shutdown();

  // This function initializes the various components within the display subsytem such as
  // Overlay Engine, RDMA Engine, DSI, HDMI, etc
  zx_status_t DisplaySubsystemInit();
  zx_status_t CreateAndInitDisplaySubsystems();

  // This function safely and properly shutsdown the display substem. Proper shutdown of the
  // display subsystem before bringing it back up is needed to ensure sanity of all the various
  // display subsystems
  zx_status_t ShutdownDisplaySubsytem();

  zx_status_t StartupDisplaySubsytem();

  // Zircon handles
  zx::bti bti_;

  // Thread handles
  thrd_t vsync_thread_;

  // Protocol handles
  pdev_protocol_t pdev_ = {};
  zx_device_t* pdev_device_;
  sysmem_protocol_t sysmem_ = {};

  // Board Info
  pdev_board_info_t board_info_;

  // Interrupts
  zx::interrupt vsync_irq_;

  // Locks used by the display driver
  fbl::Mutex display_lock_;  // general display state (i.e. display_id)
  fbl::Mutex image_lock_;    // used for accessing imported_images_

  // display dimensions and format
  uint32_t width_;
  uint32_t height_;

  const display_setting_t* init_disp_table_ = nullptr;

  bool full_init_done_ TA_GUARDED(display_lock_) = false;

  uint8_t pending_config_ TA_GUARDED(display_lock_) = 0;
  std::array<OvlConfig, kMaxLayer> ovl_config_ TA_GUARDED(display_lock_);

  uint32_t panel_type_;

  // Display structure used by various layers of display controller
  display_setting_t disp_setting_;

  // Imported Images
  fbl::DoublyLinkedList<std::unique_ptr<ImageInfo>> imported_images_ TA_GUARDED(image_lock_);

  // Display controller related data
  ddk::DisplayControllerInterfaceProtocolClient dc_intf_ TA_GUARDED(display_lock_);

  // SMI
  std::unique_ptr<ddk::MmioBuffer> smi_mmio_;

  // DSIIMPL Protocol
  ddk::DsiImplProtocolClient dsiimpl_;

  ddk::GpioProtocolClient gpio_;

  ddk::PowerProtocolClient power_;

  // Objects
  std::unique_ptr<mt8167s_display::MtSysConfig> syscfg_;
  std::unique_ptr<mt8167s_display::Ovl> ovl_;
  std::unique_ptr<mt8167s_display::Color> color_;
  std::unique_ptr<mt8167s_display::Ccorr> ccorr_;
  std::unique_ptr<mt8167s_display::Aal> aal_;
  std::unique_ptr<mt8167s_display::Gamma> gamma_;
  std::unique_ptr<mt8167s_display::Dither> dither_;
  std::unique_ptr<mt8167s_display::DispRdma> disp_rdma_;
  std::unique_ptr<mt8167s_display::MtDsiHost> dsi_host_;
};

}  // namespace mt8167s_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_MT8167S_DISPLAY_MT8167S_DISPLAY_H_
