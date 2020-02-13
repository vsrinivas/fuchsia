// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_ASTRO_DISPLAY_AML_DSI_HOST_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_ASTRO_DISPLAY_AML_DSI_HOST_H_

#include <unistd.h>
#include <zircon/compiler.h>

#include <memory>
#include <optional>

#include <ddk/protocol/dsiimpl.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/dsiimpl.h>
#include <hwreg/mmio.h>

#include "aml-mipi-phy.h"
#include "common.h"
#include "hhi-regs.h"
#include "lcd.h"
#include "vpu-regs.h"

namespace astro_display {

class AmlDsiHost {
 public:
  AmlDsiHost(zx_device_t* pdev_dev, zx_device_t* dsi_dev, zx_device_t* lcd_gpio_dev,
             uint32_t bitrate, uint8_t panel_type)
      : pdev_dev_(pdev_dev),
        dsi_dev_(dsi_dev),
        lcd_gpio_dev_(lcd_gpio_dev),
        bitrate_(bitrate),
        panel_type_(panel_type) {}

  // This function sets up mipi dsi interface. It includes both DWC and AmLogic blocks
  // The DesignWare setup could technically be moved to the dw_mipi_dsi driver. However,
  // given the highly configurable nature of this block, we'd have to provide a lot of
  // information to the generic driver. Therefore, it's just simpler to configure it here
  zx_status_t Init();
  zx_status_t HostOn(const display_setting_t& disp_setting);
  // This function will turn off DSI Host. It is a "best-effort" function. We will attempt
  // to shutdown whatever we can. Error during shutdown path is ignored and function proceeds
  // with shutting down.
  void HostOff(const display_setting_t& disp_setting);
  void Dump();

 private:
  void PhyEnable();
  void PhyDisable();
  zx_status_t HostModeInit(const display_setting_t& disp_setting);

  std::optional<ddk::MmioBuffer> mipi_dsi_mmio_;
  std::optional<ddk::MmioBuffer> hhi_mmio_;

  pdev_protocol_t pdev_ = {};

  ddk::DsiImplProtocolClient dsiimpl_;

  zx_device_t* pdev_dev_;
  zx_device_t* dsi_dev_;
  zx_device_t* lcd_gpio_dev_;

  uint32_t bitrate_;
  uint8_t panel_type_;

  bool initialized_ = false;
  bool host_on_ = false;

  std::unique_ptr<Lcd> lcd_;
  std::unique_ptr<AmlMipiPhy> phy_;
};

}  // namespace astro_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_ASTRO_DISPLAY_AML_DSI_HOST_H_
