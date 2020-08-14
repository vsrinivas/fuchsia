// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_RDMA_REGS_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_RDMA_REGS_H_
#include <hwreg/bitfields.h>
#include <hwreg/mmio.h>

#define VPU_RDMA_AHB_START_ADDR_MAN (0x1100 << 2)
#define VPU_RDMA_AHB_END_ADDR_MAN (0x1101 << 2)
#define VPU_RDMA_AHB_START_ADDR_1 (0x1102 << 2)
#define VPU_RDMA_AHB_END_ADDR_1 (0x1103 << 2)
#define VPU_RDMA_AHB_START_ADDR_2 (0x1104 << 2)
#define VPU_RDMA_AHB_END_ADDR_2 (0x1105 << 2)
#define VPU_RDMA_AHB_START_ADDR_3 (0x1106 << 2)
#define VPU_RDMA_AHB_END_ADDR_3 (0x1107 << 2)
#define VPU_RDMA_AHB_START_ADDR_4 (0x1108 << 2)
#define VPU_RDMA_AHB_END_ADDR_4 (0x1109 << 2)
#define VPU_RDMA_AHB_START_ADDR_5 (0x110a << 2)
#define VPU_RDMA_AHB_END_ADDR_5 (0x110b << 2)
#define VPU_RDMA_AHB_START_ADDR_6 (0x110c << 2)
#define VPU_RDMA_AHB_END_ADDR_6 (0x110d << 2)
#define VPU_RDMA_AHB_START_ADDR_7 (0x110e << 2)
#define VPU_RDMA_AHB_END_ADDR_7 (0x110f << 2)
#define VPU_RDMA_AHB_START_ADDR(x) (VPU_RDMA_AHB_START_ADDR_MAN + ((x + 1) << 3))
#define VPU_RDMA_AHB_END_ADDR(x) (VPU_RDMA_AHB_END_ADDR_MAN + ((x + 1) << 3))
#define VPU_RDMA_ACCESS_AUTO (0x1110 << 2)
#define VPU_RDMA_ACCESS_AUTO2 (0x1111 << 2)
#define VPU_RDMA_ACCESS_AUTO3 (0x1112 << 2)
#define VPU_RDMA_ACCESS_MAN (0x1113 << 2)
#define VPU_RDMA_CTRL (0x1114 << 2)
#define VPU_RDMA_STATUS (0x1115 << 2)
#define VPU_RDMA_STATUS2 (0x1116 << 2)
#define VPU_RDMA_STATUS3 (0x1117 << 2)

// VPU_RDMA_ACCESS_AUTO Bit Definition
#define RDMA_ACCESS_AUTO_INT_EN(channel) (1 << ((channel + 1) << 3))
#define RDMA_ACCESS_AUTO_WRITE(channel) (1 << ((channel + 1) + 4))

// VPU_RDMA_CTRL Bit Definition
#define RDMA_CTRL_INT_DONE(channel) (1 << (channel))

// VPU_RDMA_STATUS Bit Definition
#define RDMA_STATUS_DONE(channel) (1 << (channel))

class RdmaStatusReg : public hwreg::RegisterBase<RdmaStatusReg, uint32_t> {
 public:
  DEF_FIELD(31, 24, done);
  DEF_FIELD(7, 0, req_latch);
  static auto Get() { return hwreg::RegisterAddr<RdmaStatusReg>(VPU_RDMA_STATUS); }

  // compute a value for .done() to match given the current state of
  // VPU_RDMA_ACCESS_AUTO, AUTO2, and AUTO3.
  static uint32_t DoneFromAccessAuto(const uint32_t aa, const uint32_t aa2, const uint32_t aa3) {
    const uint32_t chn7 = ((aa3 >> 24) & 0x1) << 7;
    const uint32_t chn2 = ((aa >> 24) & 0x1) << 2;
    const uint32_t chn1 = ((aa >> 16) & 0x1) << 1;
    const uint32_t chn0 = ((aa >> 8) & 0x1) << 0;
    return chn7 | chn2 | chn1 | chn0;
  }
};

class RdmaCtrlReg : public hwreg::RegisterBase<RdmaCtrlReg, uint32_t> {
 public:
  DEF_FIELD(31, 24, clear_done);
  DEF_BIT(7, write_urgent);
  DEF_BIT(6, read_urgent);
  static auto Get() { return hwreg::RegisterAddr<RdmaCtrlReg>(VPU_RDMA_CTRL); }
};

class RdmaAccessAutoReg : public hwreg::RegisterBase<RdmaAccessAutoReg, uint32_t> {
 public:
  DEF_FIELD(31, 24, chn3_intr);
  DEF_FIELD(23, 16, chn2_intr);
  DEF_FIELD(15, 8, chn1_intr);
  DEF_BIT(7, chn3_auto_write);
  DEF_BIT(6, chn2_auto_write);
  DEF_BIT(5, chn1_auto_write);
  static auto Get() { return hwreg::RegisterAddr<RdmaAccessAutoReg>(VPU_RDMA_ACCESS_AUTO); }
};

class RdmaAccessAuto2Reg : public hwreg::RegisterBase<RdmaAccessAuto2Reg, uint32_t> {
 public:
  DEF_BIT(7, chn7_auto_write);
  static auto Get() { return hwreg::RegisterAddr<RdmaAccessAuto2Reg>(VPU_RDMA_ACCESS_AUTO2); }
};

class RdmaAccessAuto3Reg : public hwreg::RegisterBase<RdmaAccessAuto3Reg, uint32_t> {
 public:
  DEF_FIELD(31, 24, chn7_intr);
  static auto Get() { return hwreg::RegisterAddr<RdmaAccessAuto3Reg>(VPU_RDMA_ACCESS_AUTO3); }
};

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_RDMA_REGS_H_
