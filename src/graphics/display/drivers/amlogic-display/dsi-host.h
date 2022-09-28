// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_DSI_HOST_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_DSI_HOST_H_

#include <fuchsia/hardware/dsiimpl/cpp/banjo.h>
#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <lib/device-protocol/pdev.h>
#include <lib/fit/function.h>
#include <lib/zx/status.h>
#include <unistd.h>
#include <zircon/compiler.h>

#include <memory>
#include <optional>

#include <ddktl/device.h>
#include <hwreg/mmio.h>

#include "src/graphics/display/drivers/amlogic-display/common.h"
#include "src/graphics/display/drivers/amlogic-display/hhi-regs.h"
#include "src/graphics/display/drivers/amlogic-display/lcd.h"
#include "src/graphics/display/drivers/amlogic-display/mipi-phy.h"
#include "src/graphics/display/drivers/amlogic-display/panel-config.h"
#include "src/graphics/display/drivers/amlogic-display/vpu-regs.h"

namespace amlogic_display {

class DsiHost {
 public:
  // Map all necessary resources. This will not modify hardware state in any
  // way, and is thus safe to use when adopting a device that was initialized by
  // the bootloader.
  static zx::status<std::unique_ptr<DsiHost>> Create(zx_device_t* parent, uint32_t panel_type);

  // This function sets up mipi dsi interface. It includes both DWC and AmLogic blocks
  // The DesignWare setup could technically be moved to the dw_mipi_dsi driver. However,
  // given the highly configurable nature of this block, we'd have to provide a lot of
  // information to the generic driver. Therefore, it's just simpler to configure it here
  zx_status_t Enable(const display_setting_t& disp_setting, uint32_t bitrate);

  // This function will turn off DSI Host. It is a "best-effort" function. We will attempt
  // to shutdown whatever we can. Error during shutdown path is ignored and function proceeds
  // with shutting down.
  void Disable(const display_setting_t& disp_setting);
  void Dump();

  uint32_t panel_type() const { return panel_type_; }

 private:
  DsiHost(zx_device_t* parent, uint32_t panel_type);

  void PhyEnable();
  void PhyDisable();
  zx_status_t HostModeInit(const display_setting_t& disp_setting);
  // Controls the shutdown register on the DSI host side.
  void SetSignalPower(bool on);
  zx_status_t LoadPowerTable(cpp20::span<const PowerOp> commands,
                             fit::callback<zx_status_t()> power_on);
  std::optional<fdf::MmioBuffer> mipi_dsi_mmio_;
  std::optional<fdf::MmioBuffer> hhi_mmio_;

  ddk::PDev pdev_;

  ddk::DsiImplProtocolClient dsiimpl_;

  ddk::GpioProtocolClient lcd_gpio_;

  uint32_t panel_type_;
  const PanelConfig* panel_config_ = nullptr;

  bool enabled_ = false;

  std::unique_ptr<Lcd> lcd_;
  std::unique_ptr<MipiPhy> phy_;
};

}  // namespace amlogic_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_DSI_HOST_H_
