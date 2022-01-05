// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/types.h>

#include <hwreg/bitfields.h>

namespace spi {

constexpr uint32_t AML_SPI_RXDATA = 0x00;
constexpr uint32_t AML_SPI_TXDATA = 0x04;
constexpr uint32_t AML_SPI_CONREG = 0x08;
constexpr uint32_t AML_SPI_INTREG = 0x0c;
constexpr uint32_t AML_SPI_DMAREG = 0x10;
constexpr uint32_t AML_SPI_STATREG = 0x14;
constexpr uint32_t AML_SPI_PERIODREG = 0x18;
constexpr uint32_t AML_SPI_TESTREG = 0x1c;
constexpr uint32_t AML_SPI_DRADDR = 0x20;
constexpr uint32_t AML_SPI_DWADDR = 0x24;
constexpr uint32_t AML_SPI_LD_CNTL0 = 0x28;
constexpr uint32_t AML_SPI_LD_CNTL1 = 0x2c;
constexpr uint32_t AML_SPI_LD_RADDR = 0x30;
constexpr uint32_t AML_SPI_LD_WADDR = 0x34;
constexpr uint32_t AML_SPI_ENHANCE_CNTL = 0x38;
constexpr uint32_t AML_SPI_ENHANCE_CNTL1 = 0x3c;
constexpr uint32_t AML_SPI_ENHANCE_CNTL2 = 0x40;

class ConReg : public hwreg::RegisterBase<ConReg, uint32_t, hwreg::EnablePrinter> {
 public:
  enum Mode { kModeSlave = 0, kModeMaster = 1 };

  static constexpr uint32_t kDataRateMax = 0b111;

  DEF_FIELD(31, 25, burst_length);
  DEF_FIELD(24, 19, bits_per_word);
  DEF_FIELD(18, 16, data_rate);
  DEF_FIELD(13, 12, chip_select);
  DEF_FIELD(9, 8, drctl);
  DEF_BIT(7, sspol);
  DEF_BIT(6, ssctl);
  DEF_BIT(5, pha);
  DEF_BIT(4, pol);
  DEF_BIT(3, smc);
  DEF_BIT(2, xch);
  DEF_BIT(1, mode);
  DEF_BIT(0, en);

  static auto Get() { return hwreg::RegisterAddr<ConReg>(AML_SPI_CONREG); }
};

class IntReg : public hwreg::RegisterBase<IntReg, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_BIT(7, tcen);
  DEF_BIT(5, rfen);
  DEF_BIT(3, rren);
  DEF_BIT(2, tfen);
  DEF_BIT(0, teen);

  static auto Get() { return hwreg::RegisterAddr<IntReg>(AML_SPI_INTREG); }
};

class DmaReg : public hwreg::RegisterBase<DmaReg, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_FIELD(31, 26, burst_number);
  DEF_FIELD(25, 20, thread_id);
  DEF_BIT(19, urgent);
  DEF_FIELD(18, 15, write_request_burst_size);
  DEF_FIELD(14, 11, read_request_burst_size);
  DEF_FIELD(10, 6, rxfifo_threshold);
  DEF_FIELD(5, 1, txfifo_threshold);
  DEF_BIT(0, enable);

  static auto Get() { return hwreg::RegisterAddr<DmaReg>(AML_SPI_DMAREG); }
};

class StatReg : public hwreg::RegisterBase<StatReg, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_BIT(7, tc);
  DEF_BIT(5, rf);
  DEF_BIT(3, rr);
  DEF_BIT(2, tf);
  DEF_BIT(0, te);

  static auto Get() { return hwreg::RegisterAddr<StatReg>(AML_SPI_STATREG); }
};

class PeriodReg : public hwreg::RegisterBase<PeriodReg, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_FIELD(14, 0, period);

  static auto Get() { return hwreg::RegisterAddr<PeriodReg>(AML_SPI_PERIODREG); }
};

class TestReg : public hwreg::RegisterBase<TestReg, uint32_t, hwreg::EnablePrinter> {
 public:
  static constexpr uint32_t kDefaultDlyctl = 0x15;

  DEF_BIT(24, clk_free_en);
  DEF_FIELD(23, 22, fiforst);
  DEF_FIELD(21, 16, dlyctl);
  DEF_BIT(15, swap);
  DEF_BIT(14, lbc);
  DEF_FIELD(12, 10, smstatus);
  DEF_FIELD(9, 5, rxcnt);
  DEF_FIELD(4, 0, txcnt);

  static auto Get() { return hwreg::RegisterAddr<TestReg>(AML_SPI_TESTREG); }
  static auto GetFromDefaultValue() { return Get().FromValue(0).set_dlyctl(kDefaultDlyctl); }
};

class LdCntl0 : public hwreg::RegisterBase<LdCntl0, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_BIT(8, addr_load_signal);
  DEF_BIT(7, waddr_load_vsync);
  DEF_BIT(6, raddr_load_vsync);
  DEF_BIT(5, write_counter_enable);
  DEF_BIT(4, read_counter_enable);
  DEF_BIT(3, xch_enable_by_vsync);
  DEF_BIT(2, dma_enable_by_vsync);
  DEF_BIT(0, vsync_source);

  static auto Get() { return hwreg::RegisterAddr<LdCntl0>(AML_SPI_LD_CNTL0); }
};

class LdCntl1 : public hwreg::RegisterBase<LdCntl1, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_FIELD(31, 16, dma_write_counter);
  DEF_FIELD(15, 0, dma_read_counter);

  static auto Get() { return hwreg::RegisterAddr<LdCntl1>(AML_SPI_LD_CNTL1); }
};

class EnhanceCntl : public hwreg::RegisterBase<EnhanceCntl, uint32_t, hwreg::EnablePrinter> {
 public:
  enum SpiClkSelect { kConReg = 0, kEnhanceCntl = 1 };

  static constexpr uint32_t kEnhanceClkDivMax = 0xff;

  DEF_BIT(29, main_clock_always_on);
  DEF_BIT(28, clk_cs_delay_enable);
  DEF_BIT(27, cs_oen_enhance_enable);
  DEF_BIT(26, clk_oen_enhance_enable);
  DEF_BIT(25, mosi_oen_enhance_enable);
  DEF_BIT(24, spi_clk_select);
  DEF_FIELD(23, 16, enhance_clk_div);
  DEF_FIELD(15, 0, clk_cs_delay);

  static auto Get() { return hwreg::RegisterAddr<EnhanceCntl>(AML_SPI_ENHANCE_CNTL); }
};

class EnhanceCntl1 : public hwreg::RegisterBase<EnhanceCntl1, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_FIELD(31, 29, enhance_fclk_mosi_oen_dlyctl);
  DEF_BIT(28, enhance_fclk_mosi_oen_dlyctl_en);
  DEF_FIELD(27, 25, enhance_fclk_mosi_o_dlyctl);
  DEF_BIT(24, enhance_fclk_mosi_o_dlyctl_en);
  DEF_FIELD(23, 21, enhance_fclk_miso_i_dlyctl);
  DEF_BIT(20, enhance_fclk_miso_i_dlyctl_en);
  DEF_FIELD(19, 17, enhance_fclk_mosi_i_dlyctl);
  DEF_BIT(16, enhance_fclk_mosi_i_dlyctl_en);
  DEF_BIT(15, enhance_fclk_en);
  DEF_BIT(14, enhance_mosi_i_capture_en);
  DEF_FIELD(9, 1, enhance_clk_tcnt);
  DEF_BIT(0, enhance_miso_i_capture_en);

  static auto Get() { return hwreg::RegisterAddr<EnhanceCntl1>(AML_SPI_ENHANCE_CNTL1); }
};

class EnhanceCntl2 : public hwreg::RegisterBase<EnhanceCntl2, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_BIT(31, clk_cs_tt_delay_enable);
  DEF_FIELD(30, 16, clk_cs_tt_delay_value);
  DEF_BIT(15, clk_cs_ti_delay_enable);
  DEF_FIELD(14, 0, clk_cs_ti_delay_value);

  static auto Get() { return hwreg::RegisterAddr<EnhanceCntl2>(AML_SPI_ENHANCE_CNTL2); }
};

}  // namespace spi
