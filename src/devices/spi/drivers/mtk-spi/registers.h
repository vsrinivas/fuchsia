// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SPI_DRIVERS_MTK_SPI_REGISTERS_H_
#define SRC_DEVICES_SPI_DRIVERS_MTK_SPI_REGISTERS_H_

#include <zircon/types.h>

#include <hwreg/bitfields.h>

namespace spi {

constexpr uint32_t MTK_SPI_CFG0 = 0x00;
constexpr uint32_t MTK_SPI_CFG1 = 0x04;
constexpr uint32_t MTK_SPI_TX_SRC = 0x08;
constexpr uint32_t MTK_SPI_RX_DST = 0x0c;
constexpr uint32_t MTK_SPI_TX_DATA = 0x10;
constexpr uint32_t MTK_SPI_RX_DATA = 0x14;
constexpr uint32_t MTK_SPI_CMD = 0x18;
constexpr uint32_t MTK_SPI_STATUS0 = 0x1c;
constexpr uint32_t MTK_SPI_STATUS1 = 0x20;
constexpr uint32_t MTK_SPI_PAD_MACRO_SEL = 0x24;
constexpr uint32_t MTK_SPI_CFG2 = 0x28;
constexpr uint32_t MTK_SPI_TX_SRC_64 = 0x2c;
constexpr uint32_t MTK_SPI_RX_DST_64 = 0x30;

class Cfg0Reg : public hwreg::RegisterBase<Cfg0Reg, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_FIELD(31, 16, cs_setup_count);
  DEF_FIELD(15, 0, cs_hold_count);

  static auto Get() { return hwreg::RegisterAddr<Cfg0Reg>(MTK_SPI_CFG0); }
};

class Cfg1Reg : public hwreg::RegisterBase<Cfg1Reg, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_FIELD(31, 29, get_tick_delay);
  DEF_FIELD(25, 16, packet_length);
  DEF_FIELD(15, 8, packet_loop_count);
  DEF_FIELD(7, 0, cs_idle_count);

  static auto Get() { return hwreg::RegisterAddr<Cfg1Reg>(MTK_SPI_CFG1); }
};

class CmdReg : public hwreg::RegisterBase<CmdReg, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_BIT(17, pause_interrupt_en);
  DEF_BIT(16, finish_interrupt_en);
  DEF_BIT(15, tx_endian);
  DEF_BIT(14, rx_endian);
  DEF_BIT(13, rx_msb_first);
  DEF_BIT(12, tx_msb_first);
  DEF_BIT(11, tx_dma_en);
  DEF_BIT(10, rx_dma_en);
  DEF_BIT(9, cpol);
  DEF_BIT(8, cpha);
  DEF_BIT(7, cs_pol);
  DEF_BIT(6, sample_sel);
  DEF_BIT(5, cs_deassert_en);
  DEF_BIT(4, pause_en);
  DEF_BIT(2, reset);
  DEF_BIT(1, resume);
  DEF_BIT(0, activate);

  static auto Get() { return hwreg::RegisterAddr<CmdReg>(MTK_SPI_CMD); }
};

class Status0Reg : public hwreg::RegisterBase<Status0Reg, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_BIT(1, pause);
  DEF_BIT(0, finish);

  static auto Get() { return hwreg::RegisterAddr<Status0Reg>(MTK_SPI_STATUS0); }
};

class Status1Reg : public hwreg::RegisterBase<Status1Reg, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_BIT(0, busy);

  static auto Get() { return hwreg::RegisterAddr<Status1Reg>(MTK_SPI_STATUS1); }
};

class PadMacroSelReg : public hwreg::RegisterBase<PadMacroSelReg, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_FIELD(2, 0, pad_macro_sel);

  static auto Get() { return hwreg::RegisterAddr<PadMacroSelReg>(MTK_SPI_PAD_MACRO_SEL); }
};

class Cfg2Reg : public hwreg::RegisterBase<Cfg2Reg, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_FIELD(31, 16, sck_low_count);
  DEF_FIELD(15, 0, sck_high_count);

  static auto Get() { return hwreg::RegisterAddr<Cfg2Reg>(MTK_SPI_CFG2); }
};

class TxSrc64Reg : public hwreg::RegisterBase<TxSrc64Reg, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_FIELD(3, 0, tx_src_64);

  static auto Get() { return hwreg::RegisterAddr<TxSrc64Reg>(MTK_SPI_TX_SRC_64); }
};

class RxDst64Reg : public hwreg::RegisterBase<RxDst64Reg, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_FIELD(3, 0, rx_dst_64);

  static auto Get() { return hwreg::RegisterAddr<RxDst64Reg>(MTK_SPI_RX_DST_64); }
};

}  // namespace spi

#endif  // SRC_DEVICES_SPI_DRIVERS_MTK_SPI_REGISTERS_H_
