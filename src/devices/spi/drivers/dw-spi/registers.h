// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/types.h>

#include <hwreg/bitfields.h>

namespace spi {

constexpr uint32_t DW_SPI_CTRL0 = 0x00;
constexpr uint32_t DW_SPI_CTRL1 = 0x04;
constexpr uint32_t DW_SPI_SSIENR = 0x08;
constexpr uint32_t DW_SPI_MWCR = 0x0c;
constexpr uint32_t DW_SPI_SER = 0x10;
constexpr uint32_t DW_SPI_BAUDR = 0x14;
constexpr uint32_t DW_SPI_TXFLTR = 0x18;
constexpr uint32_t DW_SPI_RXFLTR = 0x1c;
constexpr uint32_t DW_SPI_TXFLR = 0x20;
constexpr uint32_t DW_SPI_RXFLR = 0x24;
constexpr uint32_t DW_SPI_SR = 0x28;
constexpr uint32_t DW_SPI_IMR = 0x2c;
constexpr uint32_t DW_SPI_ISR = 0x30;
constexpr uint32_t DW_SPI_RISR = 0x34;
constexpr uint32_t DW_SPI_TXOICR = 0x38;
constexpr uint32_t DW_SPI_RXOICR = 0x3c;
constexpr uint32_t DW_SPI_RXUICR = 0x40;
constexpr uint32_t DW_SPI_MSTICR = 0x44;
constexpr uint32_t DW_SPI_ICR = 0x48;
constexpr uint32_t DW_SPI_DMACR = 0x4c;
constexpr uint32_t DW_SPI_DMATDLR = 0x50;
constexpr uint32_t DW_SPI_DMARDLR = 0x54;
constexpr uint32_t DW_SPI_IDR = 0x58;
constexpr uint32_t DW_SPI_VERSION = 0x5c;
constexpr uint32_t DW_SPI_DR = 0x60;

class Ctrl0 : public hwreg::RegisterBase<Ctrl0, uint32_t, hwreg::EnablePrinter> {
 public:
  enum Mode { kModeSlave = 0, kModeMaster = 1 };

  static constexpr uint32_t FRF_SPI = 0x0;
  static constexpr uint32_t FRF_SSP = 0x1;
  static constexpr uint32_t FRF_MICROWIRE = 0x2;

  static constexpr uint32_t TMOD_TR = 0x0; // transmit & receive
  static constexpr uint32_t TMOD_TO = 0x1; // transmit only
  static constexpr uint32_t TMOD_RO = 0x2; // receive only
  static constexpr uint32_t TMOD_EPROMREAD = 0x3; // eeprom read

  DEF_BIT(12, cfs);
  DEF_BIT(11, srl);
  DEF_BIT(10, slvoe);
  DEF_FIELD(9, 8, tmod);
  DEF_BIT(7, scol);
  DEF_BIT(6, scph);
  DEF_FIELD(5, 4, frf);
  DEF_FIELD(3, 0, dfs);

  static auto Get() { return hwreg::RegisterAddr<Ctrl0>(DW_SPI_CTRL0); }
};

class Enable : public hwreg::RegisterBase<Enable, uint32_t, hwreg::EnablePrinter> {
 public:
  static auto Get() { return hwreg::RegisterAddr<Enable>(DW_SPI_SSIENR); }
};

class ChipEnable : public hwreg::RegisterBase<ChipEnable, uint32_t, hwreg::EnablePrinter> {
 public:
  static auto Get() { return hwreg::RegisterAddr<ChipEnable>(DW_SPI_SER); }
};

class BaudRate : public hwreg::RegisterBase<BaudRate, uint32_t, hwreg::EnablePrinter> {
 public:
  static auto Get() { return hwreg::RegisterAddr<BaudRate>(DW_SPI_BAUDR); }
};

class TXFLTR : public hwreg::RegisterBase<TXFLTR, uint32_t, hwreg::EnablePrinter> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TXFLTR>(DW_SPI_TXFLTR); }
};

class Status : public hwreg::RegisterBase<Status, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_BIT(6, dcol);
  DEF_BIT(5, tx_err);
  DEF_BIT(4, rf_full);
  DEF_BIT(3, rf_not_empty);
  DEF_BIT(2, tf_empty);
  DEF_BIT(1, tf_not_full);
  DEF_BIT(0, busy);

  static auto Get() { return hwreg::RegisterAddr<Status>(DW_SPI_SR); }
};

class IMR : public hwreg::RegisterBase<IMR, uint32_t, hwreg::EnablePrinter> {
 public:
  static auto Get() { return hwreg::RegisterAddr<IMR>(DW_SPI_IMR); }
};

class Data : public hwreg::RegisterBase<Data, uint32_t, hwreg::EnablePrinter> {
 public:
  static auto Get() { return hwreg::RegisterAddr<Data>(DW_SPI_DR); }
};

}  // namespace spi
