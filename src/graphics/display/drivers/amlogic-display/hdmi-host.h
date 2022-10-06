// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_HDMI_HOST_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_HDMI_HOST_H_

#include <fidl/fuchsia.hardware.hdmi/cpp/wire.h>
#include <fuchsia/hardware/display/controller/cpp/banjo.h>
#include <fuchsia/hardware/hdmi/cpp/banjo.h>
#include <fuchsia/hardware/i2cimpl/cpp/banjo.h>
#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>

#include "src/graphics/display/drivers/amlogic-display/common.h"

namespace amlogic_display {

using fuchsia_hardware_hdmi::wire::ColorDepth;
using fuchsia_hardware_hdmi::wire::ColorFormat;
using fuchsia_hardware_hdmi::wire::ColorParam;
using fuchsia_hardware_hdmi::wire::DisplayMode;
using fuchsia_hardware_hdmi::wire::EdidOp;

#define VID_PLL_DIV_1 0
#define VID_PLL_DIV_2 1
#define VID_PLL_DIV_3 2
#define VID_PLL_DIV_3p5 3
#define VID_PLL_DIV_3p75 4
#define VID_PLL_DIV_4 5
#define VID_PLL_DIV_5 6
#define VID_PLL_DIV_6 7
#define VID_PLL_DIV_6p25 8
#define VID_PLL_DIV_7 9
#define VID_PLL_DIV_7p5 10
#define VID_PLL_DIV_12 11
#define VID_PLL_DIV_14 12
#define VID_PLL_DIV_15 13
#define VID_PLL_DIV_2p5 14

enum viu_type {
  VIU_ENCL = 0,
  VIU_ENCI,
  VIU_ENCP,
  VIU_ENCT,
};

struct pll_param {
  uint32_t mode;
  uint32_t viu_channel;
  uint32_t viu_type;
  uint32_t hpll_clk_out;
  uint32_t od1;
  uint32_t od2;
  uint32_t od3;
  uint32_t vid_pll_div;
  uint32_t vid_clk_div;
  uint32_t hdmi_tx_pixel_div;
  uint32_t encp_div;
  uint32_t enci_div;
};

struct cea_timing {
  bool interlace_mode;
  uint32_t pfreq;
  uint8_t ln;
  uint8_t pixel_repeat;
  uint8_t venc_pixel_repeat;

  uint32_t hfreq;
  uint32_t hactive;
  uint32_t htotal;
  uint32_t hblank;
  uint32_t hfront;
  uint32_t hsync;
  uint32_t hback;
  bool hpol;

  uint32_t vfreq;
  uint32_t vactive;
  uint32_t vtotal;
  uint32_t vblank0;  // in case of interlace
  uint32_t vblank1;  // vblank0 + 1 for interlace
  uint32_t vfront;
  uint32_t vsync;
  uint32_t vback;
  bool vpol;
};

struct hdmi_param {
  uint8_t phy_mode;
  pll_param pll_p_24b;
  struct cea_timing timings;
};

// HdmiHost has access to the amlogic/designware HDMI block and controls its
// operation. It also handles functions and keeps track of data that the
// amlogic/designware block does not need to know about, including clock
// calculations (which may move out of the host after fxb/69072 is resolved),
// VPU and HHI register handling, HDMI parameters, etc.
class HdmiHost {
 public:
  explicit HdmiHost(zx_device_t* parent, fidl::ClientEnd<fuchsia_hardware_hdmi::Hdmi>&& chan)
      : pdev_(ddk::PDev::FromFragment(parent)), hdmi_(std::move(chan)) {}

  zx_status_t Init();
  zx_status_t HostOn();
  void HostOff();
  zx_status_t ModeSet(const display_mode_t& mode);
  zx_status_t EdidTransfer(uint32_t bus_id, const i2c_impl_op_t* op_list, size_t op_count);

  void UpdateOutputColorFormat(ColorFormat output_color_format) {
    color_.output_color_format = output_color_format;
  }

  zx_status_t GetVic(const display_mode_t* disp_timing);
  zx_status_t GetVic(display_mode_t* disp_timing);

  zx_status_t ConfigurePll();

 private:
  zx_status_t GetVic(display_mode_t* disp_timing, hdmi_param* p);
  void ConfigEncoder();
  void ConfigPhy();

  void ConfigureHpllClkOut(uint32_t hpll);
  void ConfigureOd3Div(uint32_t div_sel);
  void WaitForPllLocked();

  ddk::PDev pdev_;

  fidl::WireSyncClient<fuchsia_hardware_hdmi::Hdmi> hdmi_;

  std::optional<fdf::MmioBuffer> vpu_mmio_;
  std::optional<fdf::MmioBuffer> hhi_mmio_;
  std::optional<fdf::MmioBuffer> cbus_mmio_;

  hdmi_param p_;
  ColorParam color_{
      .input_color_format = ColorFormat::kCf444,
      .output_color_format = ColorFormat::kCf444,
      .color_depth = ColorDepth::kCd24B,
  };
};

}  // namespace amlogic_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_HDMI_HOST_H_
