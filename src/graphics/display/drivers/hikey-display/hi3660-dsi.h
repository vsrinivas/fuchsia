// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_HIKEY_DISPLAY_HI3660_DSI_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_HIKEY_DISPLAY_HI3660_DSI_H_

#include <ddk/mmio-buffer.h>
#include <ddk/protocol/dsiimpl.h>
#include <ddktl/device.h>
#include <ddktl/protocol/dsiimpl.h>
#include <lib/mipi-dsi/mipi-dsi.h>
#include <lib/mmio/mmio.h>

#include <optional>

#include "common.h"
#include "edid.h"
#include "hidisplay.h"

namespace hi_display {

class HiDsi {
 public:
  zx_status_t DsiInit(zx_device_t* parent);
  zx_status_t DsiMipiInit();
  zx_status_t DsiGetDisplayTiming();
  void DsiDphyWrite(uint32_t reg, uint32_t val);
  void DsiConfigureDphyPll();
  zx_status_t DsiConfigureDphy();
  zx_status_t GetDisplayResolution(uint32_t& width, uint32_t& height);

 private:
  zx_status_t DsiHostConfig(const display_setting_t& disp_setting);
  zx_status_t HiDsiGetDisplaySetting(display_setting_t&);

  ddk::DsiImplProtocolClient dsiimpl_;
  DetailedTiming* std_raw_dtd_;
  DisplayTiming* std_disp_timing_;
  DetailedTiming* raw_dtd_;
  DisplayTiming* disp_timing_;
  HiEdid* edid_;
};
}  // namespace hi_display

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_HIKEY_DISPLAY_HI3660_DSI_H_
