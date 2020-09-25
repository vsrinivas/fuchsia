// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_REALTEK_RTL88XX_RTL88XX_REGISTERS_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_REALTEK_RTL88XX_RTL88XX_REGISTERS_H_

#include "register.h"

// This file contains the I/O register definitions common to all rtl88xx chips. The naming of
// classes, enumerations, and member bitfields is idiosyncratic but in accordance with their names
// in the Realtek halmac interface.

namespace wlan {
namespace rtl88xx {
namespace reg {

enum BurstSize : uint32_t {
  BURST_SIZE_3_0 = 0x0,
  BURST_SIZE_2_0_HS = 0x1,
  BURST_SIZE_2_0_FS = 0x2,
  BURST_SIZE_2_0_OTHERS = 0x3,
};

class SYS_FUNC_EN : public Register<uint16_t, 0x0002> {
 public:
  static constexpr const char* name() { return "SYS_FUNC_EN"; }
  WLAN_BIT_FIELD(fen_bbrstb, 0, 1)
  WLAN_BIT_FIELD(fen_bb_glb_rstn, 1, 1)
};

class RSV_CTRL : public Register<uint32_t, 0x001C> {
 public:
  static constexpr const char* name() { return "RSV_CTRL"; }
  WLAN_BIT_FIELD(wlock_all, 0, 1)
  WLAN_BIT_FIELD(wlock_00, 1, 1)
  WLAN_BIT_FIELD(wlock_04, 2, 1)
  WLAN_BIT_FIELD(wlock_08, 3, 1)
  WLAN_BIT_FIELD(wlock_40, 4, 1)
  WLAN_BIT_FIELD(wlock_1c_b6, 5, 1)
  WLAN_BIT_FIELD(r_dis_prst, 6, 1)
  WLAN_BIT_FIELD(lock_all_en, 7, 1)
};

class RF_CTRL : public Register<uint8_t, 0x001F> {
 public:
  static constexpr const char* name() { return "RF_CTRL"; }
  WLAN_BIT_FIELD(rf_en, 0, 1)
  WLAN_BIT_FIELD(rf_rstb, 1, 1)
  WLAN_BIT_FIELD(rf_sdmrstb, 2, 1)
};

class GPIO_MUXCFG : public Register<uint32_t, 0x0040> {
 public:
  static constexpr const char* name() { return "GPIO_MUXCFG"; }
  WLAN_BIT_FIELD(wlrfe_4_5_en, 2, 1)
};

class LED_CFG : public Register<uint32_t, 0x004C> {
 public:
  static constexpr const char* name() { return "LED_CFG"; }
  WLAN_BIT_FIELD(pape_sel_en, 25, 1)
  WLAN_BIT_FIELD(lnaon_sel_en, 26, 1)
};

class PAD_CTRL1 : public Register<uint32_t, 0x0064> {
 public:
  static constexpr const char* name() { return "PAD_CTRL1"; }
  WLAN_BIT_FIELD(lnaon_wlbt_sel, 28, 1)
  WLAN_BIT_FIELD(pape_wlbt_sel, 29, 1)
};

class WLRF1 : public Register<uint32_t, 0x00EC> {
 public:
  static constexpr const char* name() { return "WLRF1"; }
  WLAN_BIT_FIELD(wlrf1_ctrl, 24, 8)
};

class SYS_CFG1 : public Register<uint32_t, 0x00F0> {
 public:
  static constexpr const char* name() { return "SYS_CFG1"; }
};

class SYS_CFG2 : public Register<uint32_t, 0x00FC> {
 public:
  static constexpr const char* name() { return "SYS_CFG2"; }
  enum HwId : uint32_t {
    HW_ID_8821C = 0x09,
  };
  WLAN_BIT_FIELD(hw_id, 0, 8)
  WLAN_BIT_FIELD(u3_term_detect, 29, 1)
};

class TXDMA_OFFSET_CHK : public Register<uint32_t, 0x020C> {
 public:
  static constexpr const char* name() { return "TXDMA_OFFSET_CHK"; }
  WLAN_BIT_FIELD(drop_data_en, 9, 1)
};

class RXDMA_MODE : public Register<uint32_t, 0x0290> {
 public:
  static constexpr const char* name() { return "RXDMA_MODE"; }
  WLAN_BIT_FIELD(dma_mode, 1, 1)
  WLAN_BIT_FIELD(burst_cnt, 2, 2)
  WLAN_BIT_FIELD(burst_size, 4, 2)  // BURST_SIZE_*
};

class USB_USBSTAT : public Register<uint8_t, 0xFE11> {
 public:
  static constexpr const char* name() { return "USB_USBSTAT"; }
  WLAN_BIT_FIELD(burst_size, 0, 2)  // BURST_SIZE_*, not documented in halmac.
};

class USB_DMA_AGG_TO : public Register<uint8_t, 0xFE5B> {
 public:
  static constexpr const char* name() { return "USB_DMA_AGG_TO"; }
  WLAN_BIT_FIELD(bit_4, 4, 1)  // Not documented in halmac.
};

}  // namespace reg
}  // namespace rtl88xx
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_REALTEK_RTL88XX_RTL88XX_REGISTERS_H_
