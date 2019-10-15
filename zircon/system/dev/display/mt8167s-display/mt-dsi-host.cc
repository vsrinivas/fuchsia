// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mt-dsi-host.h"

#include <memory>

#include <fbl/alloc_checker.h>

namespace mt8167s_display {

namespace {
constexpr uint32_t kMaxDataRate = 1250;
constexpr uint32_t kSscDelta1 = 5;
constexpr uint32_t kNum1 = 262144;
constexpr uint32_t kNum2 = 281664;
constexpr uint32_t kDen1 = 563329;
constexpr uint32_t kSdmSscPrd = 0x1B1;
constexpr uint32_t kPllCon2DefaultValue = 0x50000000;
constexpr uint32_t kDsiStartOffset = 0;
constexpr uint32_t kDsiStartEn = 1;
}  // namespace

zx_status_t MtDsiHost::Init(const ddk::DsiImplProtocolClient* dsi,
                            const ddk::GpioProtocolClient* gpio,
                            const ddk::PowerProtocolClient* power) {
  if (initialized_) {
    return ZX_OK;
  }

  dsiimpl_ = *dsi;
  power_ = *power;

  // Map MIPI TX
  mmio_buffer_t mmio;
  auto status =
      pdev_map_mmio_buffer(&pdev_, MMIO_DISP_MIPITX, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map MIPI TX mmio\n");
    return status;
  }
  fbl::AllocChecker ac;
  mipi_tx_mmio_ = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, mmio);
  if (!ac.check()) {
    DISP_ERROR("Could not map MIPI TX mmio\n");
    return ZX_ERR_NO_MEMORY;
  }

  // Get BTI from parent
  status = pdev_get_bti(&pdev_, 0, bti_.reset_and_get_address());
  if (status != ZX_OK) {
    DISP_ERROR("Could not get BTI handle\n");
    return status;
  }

  // Load LCD Init values while in command mode
  lcd_ = fbl::make_unique_checked<mt8167s_display::Lcd>(&ac, dsi, gpio, panel_type_);
  if (!ac.check()) {
    DISP_ERROR("Failed to create LCD object\n");
    return ZX_ERR_NO_MEMORY;
  }

  // MtDsiHost is ready to be used
  initialized_ = true;
  return ZX_OK;
}

void MtDsiHost::ConfigMipiPll(uint32_t pll_clock, uint32_t lane_num) {
  ZX_DEBUG_ASSERT(initialized_);
  // The programmig sequence is defined in the datasheet. However, the actual programing
  // done by the bootloader is slightly different. Will follow the same steps taken by the
  // bootloader since we know it actually works

  // Configure DSI HS impedance calibration code and enable HS Bias
  MipiTxTopConReg::Get()
      .ReadFrom(&(*mipi_tx_mmio_))
      .set_imp_cal_code(0x8)
      .set_hs_bias_en(1)
      .WriteTo(&(*mipi_tx_mmio_));

  // Setup output volatage values and enable bg core and clocks
  MipiTxBgConReg::Get()
      .ReadFrom(&(*mipi_tx_mmio_))
      .set_v02_sel(0x4)
      .set_v032_sel(0x4)
      .set_v04_sel(0x4)
      .set_v072_sel(0x4)
      .set_v10_sel(0x4)
      .set_v12_sel(0x4)
      .set_bg_cken(1)
      .set_bg_core_en(1)
      .WriteTo(&(*mipi_tx_mmio_));

  // Delay for 10us
  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));

  // Enable CKG LDO Output and LDO Core
  MipiTxConReg::Get()
      .ReadFrom(&(*mipi_tx_mmio_))
      .set_ckg_ldoout_en(1)
      .set_ldocore_en(1)
      .WriteTo(&(*mipi_tx_mmio_));

  // PLL Power-on Control
  MipiTxPllPwrReg::Get().ReadFrom(&(*mipi_tx_mmio_)).set_sdm_pwr_on(1).WriteTo(&(*mipi_tx_mmio_));

  // Toggle PLL Isolation
  MipiTxPllPwrReg::Get().ReadFrom(&(*mipi_tx_mmio_)).set_sdm_iso_en(1).WriteTo(&(*mipi_tx_mmio_));

  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));

  MipiTxPllPwrReg::Get().ReadFrom(&(*mipi_tx_mmio_)).set_sdm_iso_en(0).WriteTo(&(*mipi_tx_mmio_));

  // Set pre and post div to zero
  MipiTxPllCon0Reg::Get()
      .ReadFrom(&(*mipi_tx_mmio_))
      .set_pre_div(0)
      .set_post_div(0)
      .WriteTo(&(*mipi_tx_mmio_));

  // Calculate PLL TX clock values based on data rate
  // Calculation below are not documented and are based on bootloader
  uint32_t datarate = pll_clock * 2;
  ZX_ASSERT(datarate < kMaxDataRate);

  uint32_t txdiv, txdiv0, txdiv1;
  if (datarate >= 500) {
    txdiv = 1;
    txdiv0 = 0;
    txdiv1 = 0;
  } else if (datarate >= 250) {
    txdiv = 2;
    txdiv0 = 1;
    txdiv1 = 0;
  } else if (datarate >= 125) {
    txdiv = 4;
    txdiv0 = 2;
    txdiv1 = 0;
  } else if (datarate > 62) {
    txdiv = 8;
    txdiv0 = 2;
    txdiv1 = 1;
  } else if (datarate >= 50) {
    txdiv = 16;
    txdiv0 = 2;
    txdiv1 = 2;
  } else {
    DISP_ERROR("Datarate too low!\n");
    ZX_ASSERT(0);
  }

  // Write txdiv0 and txdiv1
  MipiTxPllCon0Reg::Get()
      .ReadFrom(&(*mipi_tx_mmio_))
      .set_txdiv0(txdiv0)
      .set_txdiv1(txdiv1)
      .WriteTo(&(*mipi_tx_mmio_));

  // configure PLL PCW
  // pcw = datarate * 4 * txdiv / (26 * 2) [26 is the 26MHz ref clock]
  uint32_t pcw_h = (datarate * txdiv / 13) & 0x7F;
  uint32_t pcw_23_16 = (256 * (datarate * txdiv % 13) / 13) & 0xFF;
  uint32_t pcw_15_8 = (256 * (256 * (datarate * txdiv % 13) % 13) / 13) & 0xFF;
  uint32_t pcw_7_0 = (256 * (256 * (256 * (datarate * txdiv % 13) % 13) % 13) / 13) & 0xFF;
  MipiTxPllCon2Reg::Get()
      .ReadFrom(&(*mipi_tx_mmio_))
      .set_pcw_h(pcw_h)
      .set_pcw_23_16(pcw_23_16)
      .set_pcw_15_8(pcw_15_8)
      .set_pcw_7_0(pcw_7_0)
      .WriteTo(&(*mipi_tx_mmio_));

  MipiTxPllCon1Reg::Get()
      .ReadFrom(&(*mipi_tx_mmio_))
      .set_sdm_ssc_ph_init(1)
      .set_sdm_ssc_prd(kSdmSscPrd)
      .WriteTo(&(*mipi_tx_mmio_));

  uint32_t pdelta1 = (kSscDelta1 * datarate * txdiv * kNum1 + kNum2) / kDen1;
  MipiTxPllCon3Reg::Get()
      .ReadFrom(&(*mipi_tx_mmio_))
      .set_sdm_ssc_delta(pdelta1)
      .set_sdm_ssc_delta1(pdelta1)
      .WriteTo(&(*mipi_tx_mmio_));

  // Enable fractional mode
  MipiTxPllCon1Reg::Get().ReadFrom(&(*mipi_tx_mmio_)).set_sdm_fra_en(1).WriteTo(&(*mipi_tx_mmio_));

  // Configure DSI0 clock lane
  MipiTxClockLaneReg::Get()
      .ReadFrom(&(*mipi_tx_mmio_))
      .set_rt_code(0x8)
      .set_phi_sel(1)
      .set_ldoout_en(1)
      .WriteTo(&(*mipi_tx_mmio_));

  // Configure Data Lanes (1 to max)
  switch (lane_num) {
    case 4:
      MipiTxDataLane3Reg::Get()
          .ReadFrom(&(*mipi_tx_mmio_))
          .set_rt_code(0x8)
          .set_ldoout_en(1)
          .WriteTo(&(*mipi_tx_mmio_));
      __FALLTHROUGH;
    case 3:
      MipiTxDataLane2Reg::Get()
          .ReadFrom(&(*mipi_tx_mmio_))
          .set_rt_code(0x8)
          .set_ldoout_en(1)
          .WriteTo(&(*mipi_tx_mmio_));
      __FALLTHROUGH;
    case 2:
      MipiTxDataLane1Reg::Get()
          .ReadFrom(&(*mipi_tx_mmio_))
          .set_rt_code(0x8)
          .set_ldoout_en(1)
          .WriteTo(&(*mipi_tx_mmio_));
      __FALLTHROUGH;
    case 1:
      MipiTxDataLane0Reg::Get()
          .ReadFrom(&(*mipi_tx_mmio_))
          .set_rt_code(0x8)
          .set_ldoout_en(1)
          .WriteTo(&(*mipi_tx_mmio_));
      break;
    default:
      DISP_ERROR("Invalid number of data lanes (%d)\n", lane_num);
      ZX_ASSERT(0);
  }

  // Enable PLL
  MipiTxPllCon0Reg::Get().ReadFrom(&(*mipi_tx_mmio_)).set_pll_en(1).WriteTo(&(*mipi_tx_mmio_));

  // Delay for 10us
  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));

  // Ennable SSC
  MipiTxPllCon1Reg::Get().ReadFrom(&(*mipi_tx_mmio_)).set_sdm_ssc_en(1).WriteTo(&(*mipi_tx_mmio_));
  // Write to PLL Preserve
  MipiTxPllTopReg::Get().ReadFrom(&(*mipi_tx_mmio_)).set_preserve(0x3).WriteTo(&(*mipi_tx_mmio_));
  // Disable Pad Tie Low
  MipiTxTopConReg::Get()
      .ReadFrom(&(*mipi_tx_mmio_))
      .set_pad_tie_low_en(0)
      .WriteTo(&(*mipi_tx_mmio_));
}

void MtDsiHost::PowerOffMipiTx() {
  MipiTxSwCtrlCon0Reg::Get()
      .ReadFrom(&(*mipi_tx_mmio_))
      .set_sw_lntc_lptx_pre_oe(1)
      .set_sw_lntc_lptx_oe(1)
      .WriteTo(&(*mipi_tx_mmio_));

  MipiTxSwCtrlCon1Reg::Get()
      .ReadFrom(&(*mipi_tx_mmio_))
      .set_sw_lnt0_lptx_pre_oe(1)
      .set_sw_lnt0_lptx_oe(1)
      .set_sw_lnt1_lptx_pre_oe(1)
      .set_sw_lnt1_lptx_oe(1)
      .set_sw_lnt2_lptx_pre_oe(1)
      .set_sw_lnt2_lptx_oe(1)
      .set_sw_lnt3_lptx_pre_oe(1)
      .set_sw_lnt3_lptx_oe(1)
      .WriteTo(&(*mipi_tx_mmio_));

  // Enable mipi sw mode
  MipiTxSwCtrlReg::Get().ReadFrom(&(*mipi_tx_mmio_)).set_enable(1).WriteTo(&(*mipi_tx_mmio_));
  // Disable mipi clock
  MipiTxPllCon0Reg::Get().ReadFrom(&(*mipi_tx_mmio_)).set_pll_en(0).WriteTo(&(*mipi_tx_mmio_));

  zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));

  MipiTxPllTopReg::Get().ReadFrom(&(*mipi_tx_mmio_)).set_preserve(0).WriteTo(&(*mipi_tx_mmio_));

  MipiTxTopConReg::Get()
      .ReadFrom(&(*mipi_tx_mmio_))
      .set_pad_tie_low_en(1)
      .WriteTo(&(*mipi_tx_mmio_));

  MipiTxClockLaneReg::Get().ReadFrom(&(*mipi_tx_mmio_)).set_ldoout_en(0).WriteTo(&(*mipi_tx_mmio_));
  MipiTxDataLane0Reg::Get().ReadFrom(&(*mipi_tx_mmio_)).set_ldoout_en(0).WriteTo(&(*mipi_tx_mmio_));
  MipiTxDataLane1Reg::Get().ReadFrom(&(*mipi_tx_mmio_)).set_ldoout_en(0).WriteTo(&(*mipi_tx_mmio_));
  MipiTxDataLane2Reg::Get().ReadFrom(&(*mipi_tx_mmio_)).set_ldoout_en(0).WriteTo(&(*mipi_tx_mmio_));
  MipiTxDataLane3Reg::Get().ReadFrom(&(*mipi_tx_mmio_)).set_ldoout_en(0).WriteTo(&(*mipi_tx_mmio_));

  zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));

  MipiTxPllPwrReg::Get()
      .ReadFrom(&(*mipi_tx_mmio_))
      .set_sdm_iso_en(1)
      .set_sdm_pwr_on(0)
      .WriteTo(&(*mipi_tx_mmio_));
  MipiTxTopConReg::Get().ReadFrom(&(*mipi_tx_mmio_)).set_hs_bias_en(0).WriteTo(&(*mipi_tx_mmio_));
  MipiTxConReg::Get()
      .ReadFrom(&(*mipi_tx_mmio_))
      .set_ckg_ldoout_en(0)
      .set_ldocore_en(0)
      .WriteTo(&(*mipi_tx_mmio_));
  MipiTxBgConReg::Get()
      .ReadFrom(&(*mipi_tx_mmio_))
      .set_bg_cken(0)
      .set_bg_core_en(0)
      .WriteTo(&(*mipi_tx_mmio_));
  MipiTxPllCon0Reg::Get()
      .ReadFrom(&(*mipi_tx_mmio_))
      .set_post_div(0)
      .set_txdiv1(0)
      .set_txdiv0(0)
      .set_pre_div(0)
      .WriteTo(&(*mipi_tx_mmio_));

  MipiTxPllCon1Reg::Get().FromValue(0).WriteTo(&(*mipi_tx_mmio_));
  MipiTxPllCon2Reg::Get().FromValue(kPllCon2DefaultValue).WriteTo(&(*mipi_tx_mmio_));

  // Disable mipi sw mode
  MipiTxSwCtrlReg::Get().ReadFrom(&(*mipi_tx_mmio_)).set_enable(0).WriteTo(&(*mipi_tx_mmio_));

  zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
}

zx_status_t MtDsiHost::Shutdown(std::unique_ptr<MtSysConfig>& syscfg) {
  ZX_DEBUG_ASSERT(initialized_);
  if (IsHostOn()) {
    if (dsiimpl_.is_valid()) {
      dsiimpl_.PowerDown();
    }
    PowerOffMipiTx();
  }
  syscfg->PowerDown(MODULE_DSI0);
  lcd_->PowerOff();
  if (power_.is_valid()) {
    power_.DisablePowerDomain();
  }
  return ZX_OK;
}

zx_status_t MtDsiHost::PowerOn(std::unique_ptr<MtSysConfig>& syscfg) {
  ZX_DEBUG_ASSERT(initialized_);
  syscfg->PowerOn(MODULE_DSI0);
  lcd_->PowerOn();
  if (power_.is_valid()) {
    power_.EnablePowerDomain();
  }
  return ZX_OK;
}

zx_status_t MtDsiHost::Start() {
  ZX_DEBUG_ASSERT(initialized_);
  dsiimpl_.SetMode(DSI_MODE_VIDEO);
  // This will cause a trigger of the system which will get things started
  dsiimpl_.WriteReg(kDsiStartOffset, 0);
  dsiimpl_.WriteReg(kDsiStartOffset, kDsiStartEn);
  return ZX_OK;
}

zx_status_t MtDsiHost::Config(const display_setting_t& disp_setting) {
  ZX_DEBUG_ASSERT(initialized_);

  // First, configure the DSI PHY
  ConfigMipiPll(disp_setting.lcd_clock, disp_setting.lane_num);

  // Configure DSI parameters needed for DSI Video Mode
  dsi_config_t dsi_cfg;
  dsi_cfg.display_setting = disp_setting;
  dsi_cfg.video_mode_type = VIDEO_MODE_NON_BURST_PULSE;
  dsi_cfg.color_coding = COLOR_CODE_PACKED_24BIT_888;
  dsi_cfg.vendor_config_buffer = nullptr;

  // No vendor specific data for now
  dsiimpl_.Config(&dsi_cfg);

  // Configure MIPI D-PHY Timing parameters. Make sure this is called AFTER dsiimpl_.Config
  dsiimpl_.PhyPowerUp();

  dsiimpl_.PowerUp();

  dsiimpl_.SetMode(DSI_MODE_COMMAND);
  lcd_->Enable();

#if 0
    // TESTING ONLY: This will prove out whether the DSI layer + LCD panel have been configured
    // correctly regardless of the status of the upper layers of display subsystem
    dsiimpl_.EnableBist(0xffff00ff);
    while (1) {}
#endif
  return ZX_OK;
}

void MtDsiHost::PrintRegisters() {
  ZX_DEBUG_ASSERT(initialized_);
  zxlogf(INFO, "Dumping DSI MIPI PHY Registers:\n");
  zxlogf(INFO, "######################\n\n");
  zxlogf(INFO, "MIPI_TX_CON = 0x%x\n", mipi_tx_mmio_->Read32(MIPI_TX_CON));
  zxlogf(INFO, "MIPI_TX_CLOCK_LANE = 0x%x\n", mipi_tx_mmio_->Read32(MIPI_TX_CLOCK_LANE));
  zxlogf(INFO, "MIPI_TX_DATA_LANE0 = 0x%x\n", mipi_tx_mmio_->Read32(MIPI_TX_DATA_LANE0));
  zxlogf(INFO, "MIPI_TX_DATA_LANE1 = 0x%x\n", mipi_tx_mmio_->Read32(MIPI_TX_DATA_LANE1));
  zxlogf(INFO, "MIPI_TX_DATA_LANE2 = 0x%x\n", mipi_tx_mmio_->Read32(MIPI_TX_DATA_LANE2));
  zxlogf(INFO, "MIPI_TX_DATA_LANE3 = 0x%x\n", mipi_tx_mmio_->Read32(MIPI_TX_DATA_LANE3));
  zxlogf(INFO, "MIPI_TX_TOP_CON = 0x%x\n", mipi_tx_mmio_->Read32(MIPI_TX_TOP_CON));
  zxlogf(INFO, "MIPI_TX_BG_CON = 0x%x\n", mipi_tx_mmio_->Read32(MIPI_TX_BG_CON));
  zxlogf(INFO, "MIPI_TX_PLL_CON0 = 0x%x\n", mipi_tx_mmio_->Read32(MIPI_TX_PLL_CON0));
  zxlogf(INFO, "MIPI_TX_PLL_CON1 = 0x%x\n", mipi_tx_mmio_->Read32(MIPI_TX_PLL_CON1));
  zxlogf(INFO, "MIPI_TX_PLL_CON2 = 0x%x\n", mipi_tx_mmio_->Read32(MIPI_TX_PLL_CON2));
  zxlogf(INFO, "MIPI_TX_PLL_CON3 = 0x%x\n", mipi_tx_mmio_->Read32(MIPI_TX_PLL_CON3));
  zxlogf(INFO, "MIPI_TX_PLL_CHG = 0x%x\n", mipi_tx_mmio_->Read32(MIPI_TX_PLL_CHG));
  zxlogf(INFO, "MIPI_TX_PLL_TOP = 0x%x\n", mipi_tx_mmio_->Read32(MIPI_TX_PLL_TOP));
  zxlogf(INFO, "MIPI_TX_PLL_PWR = 0x%x\n", mipi_tx_mmio_->Read32(MIPI_TX_PLL_PWR));
  zxlogf(INFO, "MIPI_TX_RGS = 0x%x\n", mipi_tx_mmio_->Read32(MIPI_TX_RGS));
  zxlogf(INFO, "MIPI_TX_SW_CTRL = 0x%x\n", mipi_tx_mmio_->Read32(MIPI_TX_SW_CTRL));
  zxlogf(INFO, "MIPI_TX_SW_CTRL_CON0 = 0x%x\n", mipi_tx_mmio_->Read32(MIPI_TX_SW_CTRL_CON0));
  zxlogf(INFO, "MIPI_TX_SW_CTRL_CON1 = 0x%x\n", mipi_tx_mmio_->Read32(MIPI_TX_SW_CTRL_CON1));
  zxlogf(INFO, "######################\n\n");
  dsiimpl_.PrintDsiRegisters();
}

}  // namespace mt8167s_display
