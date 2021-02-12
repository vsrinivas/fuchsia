// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AML_HDMITX_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AML_HDMITX_H_

#include <fuchsia/hardware/display/controller/cpp/banjo.h>
#include <fuchsia/hardware/i2cimpl/cpp/banjo.h>
#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <string.h>

#include <optional>

#include <fbl/auto_lock.h>

#include "common.h"
#include "hdmitx-vpu-regs.h"

#define HDMI_COLOR_DEPTH_24B 4
#define HDMI_COLOR_DEPTH_30B 5
#define HDMI_COLOR_DEPTH_36B 6
#define HDMI_COLOR_DEPTH_48B 7

#define HDMI_COLOR_FORMAT_RGB 0
#define HDMI_COLOR_FORMAT_444 1

#define HDMI_ASPECT_RATIO_NONE 0
#define HDMI_ASPECT_RATIO_4x3 1
#define HDMI_ASPECT_RATIO_16x9 2

#define HDMI_COLORIMETRY_ITU601 1
#define HDMI_COLORIMETRY_ITU709 2

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

namespace amlogic_display {

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
  uint16_t vic;
  uint8_t aspect_ratio;
  uint8_t colorimetry;
  uint8_t phy_mode;
  struct pll_param pll_p_24b;
  struct cea_timing timings;
  bool is4K;
};

// TODO(fxb/69026): move HDMI to its own device
class AmlHdmitx {
 public:
  AmlHdmitx(zx_device_t* parent) : pdev_(ddk::PDev::FromFragment(parent)) {}

  zx_status_t Init();
  zx_status_t InitHw();
  zx_status_t InitInterface();

  void ShutDown();

  void UpdateOutputColorFormat(uint8_t output_color_format) {
    output_color_format_ = output_color_format;
  }

  zx_status_t GetVic(const display_mode_t* disp_timing);
  const display_mode_t* GetCurDisplayMode() { return &cur_display_mode_; }
  void SaveCurDisplayMode(const display_mode_t* mode) {
    memcpy(&cur_display_mode_, mode, sizeof(display_mode_t));
  }

  zx_status_t I2cImplTransact(uint32_t bus_id, const i2c_impl_op_t* op_list, size_t op_count);

 private:
  friend class AmlogicDisplay;

  zx_status_t ConfigurePll(const struct hdmi_param* p, const struct pll_param* pll);
  void ConfigureHpllClkOut(uint32_t hpll);
  void ConfigureOd3Div(uint32_t div_sel);
  void WaitForPllLocked();

  void ConfigEncoder(const struct hdmi_param* p);
  void ConfigHdmitx(const struct hdmi_param* p);
  void ConfigPhy(const struct hdmi_param* p);
  void ConfigCsc(const struct hdmi_param* p);

  void WriteReg(uint32_t addr, uint32_t data);
  uint32_t ReadReg(uint32_t addr);

  void ScdcWrite(uint8_t addr, uint8_t val);
  void ScdcRead(uint8_t addr, uint8_t* val);

  ddk::PDev pdev_;

  std::optional<ddk::MmioBuffer> vpu_mmio_;
  fbl::Mutex register_lock_;
  std::optional<ddk::MmioBuffer> hdmitx_mmio_ TA_GUARDED(register_lock_);
  std::optional<ddk::MmioBuffer> hhi_mmio_;
  std::optional<ddk::MmioBuffer> cbus_mmio_;

  uint8_t input_color_format_ = HDMI_COLOR_FORMAT_444;
  uint8_t output_color_format_ = HDMI_COLOR_FORMAT_444;
  uint8_t color_depth_ = HDMI_COLOR_DEPTH_24B;

  hdmi_param p_;
  display_mode_t cur_display_mode_;

  fbl::Mutex i2c_lock_;
};

}  // namespace amlogic_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AML_HDMITX_H_
