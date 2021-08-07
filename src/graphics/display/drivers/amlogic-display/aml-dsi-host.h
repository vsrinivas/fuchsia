// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AML_DSI_HOST_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AML_DSI_HOST_H_

#include <fuchsia/hardware/dsiimpl/cpp/banjo.h>
#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <lib/device-protocol/pdev.h>
#include <unistd.h>
#include <zircon/compiler.h>

#include <memory>
#include <optional>

#include <ddktl/device.h>
#include <hwreg/mmio.h>

#include "aml-mipi-phy.h"
#include "common.h"
#include "hhi-regs.h"
#include "lcd.h"
#include "vpu-regs.h"

namespace amlogic_display {

class AmlDsiHost {
 public:
  AmlDsiHost(zx_device_t* parent, uint32_t panel_type)
      : pdev_(ddk::PDev::FromFragment(parent)),
        dsiimpl_(parent, "dsi"),
        lcd_gpio_(parent, "gpio"),
        panel_type_(panel_type) {}

  // This function sets up mipi dsi interface. It includes both DWC and AmLogic blocks
  // The DesignWare setup could technically be moved to the dw_mipi_dsi driver. However,
  // given the highly configurable nature of this block, we'd have to provide a lot of
  // information to the generic driver. Therefore, it's just simpler to configure it here
  zx_status_t Init(uint32_t bitrate);
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
  // Rewrites panel_type_ if the display_id_ indicates that the GPIO-based
  // identification was wrong.
  void FixupPanelType();

  std::optional<ddk::MmioBuffer> mipi_dsi_mmio_;
  std::optional<ddk::MmioBuffer> hhi_mmio_;

  ddk::PDev pdev_;

  ddk::DsiImplProtocolClient dsiimpl_;

  ddk::GpioProtocolClient lcd_gpio_;

  uint32_t bitrate_;
  uint32_t panel_type_;

  // Cached 3-byte ID read from MIPI regs. This is used on products where the
  // board does not provide enough GPIO pins to distinguish all of the DDICs.
  uint32_t display_id_ = 0;

  bool initialized_ = false;
  bool host_on_ = false;

  std::unique_ptr<Lcd> lcd_;
  std::unique_ptr<AmlMipiPhy> phy_;
};

}  // namespace amlogic_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AML_DSI_HOST_H_
