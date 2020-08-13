// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_PCI_LIB_DESIGNWARE_DW_PCIE_HW_H_
#define SRC_DEVICES_PCI_LIB_DESIGNWARE_DW_PCIE_HW_H_

#include <hw/reg.h>
#include <hwreg/bitfields.h>
#include <hwreg/mmio.h>

namespace pcie {
namespace designware {

namespace PortLogic {
constexpr uint32_t Base = 0x700;
constexpr uint32_t DebugR1Offset = Base + 0x2c;
class DebugR1 : public hwreg::RegisterBase<DebugR1, uint32_t> {
 public:
  DEF_BIT(4, link_up);
  DEF_BIT(29, link_in_training);
  static auto Get() { return hwreg::RegisterAddr<DebugR1>(DebugR1Offset); }
};

}  // namespace PortLogic

#define PORT_LINK_CTRL_OFF (0x710)
#define PLC_VENDOR_SPECIFIC_DLLP_REQ (1 << 0)
#define PLC_SCRAMBLE_DISABLE (1 << 1)
#define PLC_LOOPBACK_ENABLE (1 << 2)
#define PLC_RESET_ASSERT (1 << 3)
#define PLC_DLL_LINK_EN (1 << 5)
#define PLC_LINK_DISABLE (1 << 6)
#define PLC_FAST_LINK_MODE (1 << 7)
#define PLC_LINK_RATE_MASK (0xF << 8)
#define PLC_LINK_CAPABLE_MASK (0x3F << 16)
#define PLC_LINK_CAPABLE_X1 (0x01 << 16)
#define PLC_LINK_CAPABLE_X2 (0x03 << 16)
#define PLC_LINK_CAPABLE_X4 (0x07 << 16)
#define PLC_LINK_CAPABLE_X8 (0x0f << 16)
#define PLC_LINK_CAPABLE_X16 (0x1f << 16)
#define PLC_BEACON_ENABLE (1 << 24)
#define PLC_CORRUPT_LCRC_ENABLE (1 << 25)
#define PLC_EXTENDED_SYNC_H (1 << 26)
#define PLC_TRANSMIT_LANE_REVERSAL_ENABLE (1 << 27)

#define GEN2_CTRL_OFF (0x80C)
#define G2_CTRL_FAST_TRAINING_SEQ_MASK (0xFF << 0)
#define G2_CTRL_NUM_OF_LANES_MASK (0x1F << 8)
#define G2_CTRL_NO_OF_LANES(x) ((x) << 8)
#define G2_CTRL_PRE_DET_LANE_MASK (0x07 << 13)
#define G2_CTRL_AUTO_LANE_FLIP_CTRL_EN (1 << 16)
#define G2_CTRL_DIRECT_SPEED_CHANGE (1 << 17)
#define G2_CTRL_CONFIG_PHY_TX_CHANGE (1 << 18)
#define G2_CTRL_CONFIG_TX_COMP_RX (1 << 19)
#define G2_CTRL_SEL_DEEMPHASIS (1 << 20)
#define G2_CTRL_GEN1_EI_INFERENCE (1 << 21)

#define PCIE_TLP_TYPE_MEM_RW (0x00)
#define PCIE_TLP_TYPE_MEM_RD_LOCKED (0x01)
#define PCIE_TLP_TYPE_IO_RW (0x02)
#define PCIE_TLP_TYPE_CFG0 (0x04)
#define PCIE_TLP_TYPE_CFG1 (0x05)
#define PCIE_ECAM_SIZE (0x1000)

#define PCI_TYPE1_BAR0 (0x10)
#define PCI_TYPE1_BAR1 (0x14)

const uint32_t kAtuRegionCount = (16);
const uint32_t kAtuRegionCtrlEnable = (1 << 31);
const uint32_t kAtuCfgShiftMode = (1 << 28);
const uint32_t kAtuProgramRetries = (5);
const uint32_t kAtuWaitEnableTimeoutUs = (10000);

typedef struct atu_ctrl_regs {
  uint32_t region_ctrl1;
  uint32_t region_ctrl2;
  uint32_t unroll_lower_base;
  uint32_t unroll_upper_base;
  uint32_t unroll_limit;
  uint32_t unroll_lower_target;
  uint32_t unroll_upper_target;
} __PACKED atu_ctrl_regs_t;

}  // namespace designware

}  // namespace pcie

#endif  // SRC_DEVICES_PCI_LIB_DESIGNWARE_DW_PCIE_HW_H_
