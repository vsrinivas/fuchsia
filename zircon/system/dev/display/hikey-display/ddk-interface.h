// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_HIKEY_DISPLAY_DDK_INTERFACE_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_HIKEY_DISPLAY_DDK_INTERFACE_H_

#include <lib/zircon-internal/thread_annotations.h>
#include <zircon/pixelformat.h>

#include <memory>

#include <ddk/protocol/sysmem.h>
#include <ddktl/protocol/display/controller.h>
#include <fbl/auto_lock.h>

#include "adv7533.h"
#include "hi3660-dsi.h"

constexpr uint8_t PANEL_DISPLAY_ID = 1;

namespace hi_display {

class HiDisplay;

// HiDisplay will implement only a few subset of Device.
using DeviceType = ddk::Device<HiDisplay, ddk::UnbindableNew>;

class HiDisplay : public DeviceType,
                  public ddk::DisplayControllerImplProtocol<HiDisplay, ddk::base_protocol> {
 public:
  HiDisplay(zx_device_t* parent) : DeviceType(parent) { parent_ = parent; }

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

  void DdkUnbindNew(ddk::UnbindTxn txn);
  void DdkRelease();

 private:
  int VSyncThread();
  zx_status_t SetupDisplayInterface();
  void PopulateAddedDisplayArgs(added_display_args_t* args);

  sysmem_protocol_t sysmem_ = {};

  zx::bti bti_;
  pdev_protocol_t pdev_ = {};
  zx_device_t* parent_;
  thrd_t vsync_thread_;
  std::atomic_bool vsync_shutdown_flag_ = false;

  fbl::Mutex display_lock_;
  fbl::Mutex image_lock_;

  uint64_t current_image_ TA_GUARDED(display_lock_);
  bool current_image_valid_ TA_GUARDED(display_lock_);

  // Display controller related data
  ddk::DisplayControllerInterfaceProtocolClient dc_intf_ TA_GUARDED(display_lock_);

  uint32_t width_;
  uint32_t height_;
  std::unique_ptr<hi_display::Adv7533> adv7533_;
  std::unique_ptr<hi_display::HiDsi> dsi_;
};
}  // namespace hi_display

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_HIKEY_DISPLAY_DDK_INTERFACE_H_
