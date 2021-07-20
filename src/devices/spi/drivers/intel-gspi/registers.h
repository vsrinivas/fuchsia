// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SPI_DRIVERS_INTEL_GSPI_REGISTERS_H_
#define SRC_DEVICES_SPI_DRIVERS_INTEL_GSPI_REGISTERS_H_

#include <zircon/types.h>

#include <hwreg/bitfields.h>

#include "hwreg/internal.h"

namespace gspi {

constexpr uint32_t INTEL_GSPI_SSCR0 = 0x00;
constexpr uint32_t INTEL_GSPI_SSCR1 = 0x04;
constexpr uint32_t INTEL_GSPI_SSSR = 0x08;
constexpr uint32_t INTEL_GSPI_SSDR = 0x10;
constexpr uint32_t INTEL_GSPI_SSTO = 0x28;
constexpr uint32_t INTEL_GSPI_SITF = 0x44;
constexpr uint32_t INTEL_GSPI_SIRF = 0x48;

constexpr uint32_t INTEL_GSPI_CLOCKS = 0x200;
constexpr uint32_t INTEL_GSPI_RESETS = 0x204;
constexpr uint32_t INTEL_GSPI_ACTIVELTR = 0x210;
constexpr uint32_t INTEL_GSPI_IDLELTR = 0x214;
constexpr uint32_t INTEL_GSPI_TX_BIT_COUNT = 0x218;
constexpr uint32_t INTEL_GSPI_RX_BIT_COUNT = 0x21c;
constexpr uint32_t INTEL_GSPI_SSP_REG = 0x220;
constexpr uint32_t INTEL_GSPI_CS_CONTROL = 0x224;
/* 0x228...0x234 are scratch registers */
constexpr uint32_t INTEL_GSPI_CLOCK_GATE = 0x238;
constexpr uint32_t INTEL_GSPI_REMAP_ADDR_LO = 0x240;
constexpr uint32_t INTEL_GSPI_REMAP_ADDR_HI = 0x244;
constexpr uint32_t INTEL_GSPI_DEVIDLE_CONTROL = 0x24c;
constexpr uint32_t INTEL_GSPI_DEL_RX_CLK = 0x250;
constexpr uint32_t INTEL_GSPI_CAPABILITIES = 0x2fc;

class Con0Reg : public hwreg::RegisterBase<Con0Reg, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_BIT(31, mod);
  DEF_BIT(30, acs);
  DEF_BIT(23, tim);
  DEF_BIT(22, rim);
  DEF_BIT(21, ncs);
  DEF_BIT(20, edss);
  DEF_FIELD(19, 8, scr);
  DEF_BIT(7, sse);
  DEF_BIT(6, ecs);
  DEF_FIELD(5, 4, frf);
  DEF_FIELD(3, 0, dss);

  static auto Get() { return hwreg::RegisterAddr<Con0Reg>(INTEL_GSPI_SSCR0); }
};

class Con1Reg : public hwreg::RegisterBase<Con1Reg, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_BIT(23, rwot);
  DEF_BIT(22, trail);
  DEF_BIT(21, tsre);
  DEF_BIT(20, rsre);
  DEF_BIT(19, tinte);
  DEF_BIT(16, ifs);
  DEF_BIT(4, sph);
  DEF_BIT(3, spo);
  DEF_BIT(1, tie);
  DEF_BIT(0, rie);

  static auto Get() { return hwreg::RegisterAddr<Con1Reg>(INTEL_GSPI_SSCR1); }
};

class StatusReg : public hwreg::RegisterBase<StatusReg, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_BIT(21, tur);
  DEF_BIT(19, tint);
  DEF_BIT(18, pint);
  DEF_BIT(7, ror);
  DEF_BIT(6, rfs);
  DEF_BIT(5, tfs);
  DEF_BIT(4, bsy);
  DEF_BIT(3, rne);
  DEF_BIT(2, tnf);

  static auto Get() { return hwreg::RegisterAddr<StatusReg>(INTEL_GSPI_SSSR); }
};

class FifoReg : public hwreg::RegisterBase<FifoReg, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_FIELD(31, 0, data);

  static auto Get() { return hwreg::RegisterAddr<FifoReg>(INTEL_GSPI_SSDR); }
};

class TimeoutReg : public hwreg::RegisterBase<TimeoutReg, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_FIELD(23, 0, timeout);

  static auto Get() { return hwreg::RegisterAddr<TimeoutReg>(INTEL_GSPI_SSTO); }
};

class TransmitFifoReg
    : public hwreg::RegisterBase<TransmitFifoReg, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_FIELD(21, 16, sitfl);
  DEF_FIELD(13, 8, lwmtf);
  DEF_FIELD(5, 0, hwmtf);

  static auto Get() { return hwreg::RegisterAddr<TransmitFifoReg>(INTEL_GSPI_SITF); }
};

class ReceiveFifoReg : public hwreg::RegisterBase<ReceiveFifoReg, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_FIELD(13, 8, sirfl);
  DEF_FIELD(5, 0, wmrf);

  static auto Get() { return hwreg::RegisterAddr<ReceiveFifoReg>(INTEL_GSPI_SIRF); }
};

class CSControlReg : public hwreg::RegisterBase<CSControlReg, uint32_t, hwreg::EnablePrinter> {
 public:
  enum Mode {
    kChipSelectHW = 0,
    kChipSelectSW = 1,
  };
  DEF_BIT(13, cs1_polarity);
  DEF_BIT(12, cs0_polarity);
  DEF_FIELD(9, 8, cs1_output_sel);
  DEF_BIT(1, cs_state);
  DEF_BIT(0, cs_mode);

  static auto Get() { return hwreg::RegisterAddr<CSControlReg>(INTEL_GSPI_CS_CONTROL); }
};

class ResetsReg : public hwreg::RegisterBase<ResetsReg, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_BIT(2, dma_reset);
  DEF_FIELD(1, 0, ctrl_reset);

  static auto Get() { return hwreg::RegisterAddr<ResetsReg>(INTEL_GSPI_RESETS); }
};

}  // namespace gspi

#endif  // SRC_DEVICES_SPI_DRIVERS_INTEL_GSPI_REGISTERS_H_
