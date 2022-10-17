// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/amlogic-display/dsi-host.h"

#include <lib/ddk/debug.h>
#include <lib/device-protocol/display-panel.h>

#include <fbl/alloc_checker.h>

#include "src/graphics/display/drivers/amlogic-display/common.h"
#include "src/graphics/display/drivers/amlogic-display/initcodes-inl.h"

namespace amlogic_display {

#define READ32_MIPI_DSI_REG(a) mipi_dsi_mmio_->Read32(a)
#define WRITE32_MIPI_DSI_REG(a, v) mipi_dsi_mmio_->Write32(v, a)

#define READ32_HHI_REG(a) hhi_mmio_->Read32(a)
#define WRITE32_HHI_REG(a, v) hhi_mmio_->Write32(v, a)

namespace {

constexpr uint8_t kEmptySequence[] = {};
constexpr cpp20::span<const uint8_t> kEmptySequenceSpan = {kEmptySequence, 0};

// Convenience function for building PanelConfigs. Most op sequences are shared
// between panel types.
constexpr PanelConfig MakeConfig(const char* name, cpp20::span<const uint8_t> init_seq) {
  return {name,
          init_seq,
          {lcd_shutdown_sequence, std::size(lcd_shutdown_sequence)},
          {lcd_power_on_sequence, std::size(lcd_power_on_sequence)},
          {lcd_power_off_sequence, std::size(lcd_power_off_sequence)}};
}

// LINT.IfChange
/// Panel type IDs are compact. This array should be updated when
/// <lib/device-protocol/display-panel.h> is.
PanelConfig kPanelConfig[] = {
    MakeConfig("TV070WSM_FT",
               {lcd_init_sequence_TV070WSM_FT, std::size(lcd_init_sequence_TV070WSM_FT)}),
    MakeConfig("P070ACB_FT",
               {lcd_init_sequence_P070ACB_FT, std::size(lcd_init_sequence_P070ACB_FT)}),
    MakeConfig("TV101WXM_FT",
               {lcd_init_sequence_TV101WXM_FT, std::size(lcd_init_sequence_TV101WXM_FT)}),
    MakeConfig("G101B158_FT",
               {lcd_init_sequence_G101B158_FT, std::size(lcd_init_sequence_G101B158_FT)}),
    // ILI9881C & ST7701S are not supported
    MakeConfig("ILI9881C", kEmptySequenceSpan),
    MakeConfig("ST7701S", kEmptySequenceSpan),
    MakeConfig("TV080WXM_FT",
               {lcd_init_sequence_TV080WXM_FT, std::size(lcd_init_sequence_TV080WXM_FT)}),
    MakeConfig("TV101WXM_FT_9365",
               {lcd_init_sequence_TV101WXM_FT_9365, std::size(lcd_init_sequence_TV101WXM_FT_9365)}),
    MakeConfig("TV070WSM_FT_9365",
               {lcd_init_sequence_TV070WSM_FT_9365, std::size(lcd_init_sequence_TV070WSM_FT_9365)}),
    MakeConfig("KD070D82_FT",
               {lcd_init_sequence_KD070D82_FT_9365, std::size(lcd_init_sequence_KD070D82_FT_9365)}),
    MakeConfig("KD070D82_FT_9365",
               {lcd_init_sequence_KD070D82_FT_9365, std::size(lcd_init_sequence_KD070D82_FT_9365)}),
    MakeConfig("TV070WSM_ST7703I",
               {lcd_init_sequence_TV070WSM_ST7703I, std::size(lcd_init_sequence_TV070WSM_ST7703I)}),

};
// LINT.ThenChange(//src/graphics/display/lib/device-protocol-display/include/lib/device-protocol/display-panel.h)

const PanelConfig* GetPanelConfig(uint32_t panel_type) {
  ZX_DEBUG_ASSERT(panel_type <= PANEL_TV070WSM_ST7703I);
  ZX_DEBUG_ASSERT(panel_type != PANEL_ILI9881C);
  ZX_DEBUG_ASSERT(panel_type != PANEL_ST7701S);
  if (panel_type == PANEL_ILI9881C || panel_type == PANEL_ST7701S) {
    return nullptr;
  }
  return &(kPanelConfig[panel_type]);
}

}  // namespace

DsiHost::DsiHost(zx_device_t* parent, uint32_t panel_type)
    : pdev_(ddk::PDev::FromFragment(parent)),
      dsiimpl_(parent, "dsi"),
      lcd_gpio_(parent, "gpio"),
      panel_type_(panel_type) {}

// static
zx::result<std::unique_ptr<DsiHost>> DsiHost::Create(zx_device_t* parent, uint32_t panel_type) {
  fbl::AllocChecker ac;
  std::unique_ptr<DsiHost> self =
      fbl::make_unique_checked<DsiHost>(&ac, DsiHost(parent, panel_type));
  if (!ac.check()) {
    DISP_ERROR("No memory to allocate a DSI host\n");
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  self->panel_config_ = GetPanelConfig(panel_type);
  if (self->panel_config_ == nullptr) {
    DISP_ERROR("Unrecognized panel type %d", panel_type);
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (!self->pdev_.is_valid()) {
    DISP_ERROR("DsiHost: Could not get ZX_PROTOCOL_PDEV protocol\n");
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  // Map MIPI DSI and HHI registers
  zx_status_t status = self->pdev_.MapMmio(MMIO_MPI_DSI, &(self->mipi_dsi_mmio_));
  if (status != ZX_OK) {
    DISP_ERROR("Could not map MIPI DSI mmio %s\n", zx_status_get_string(status));
    return zx::error(status);
  }

  status = self->pdev_.MapMmio(MMIO_HHI, &(self->hhi_mmio_));
  if (status != ZX_OK) {
    DISP_ERROR("Could not map HHI mmio %s\n", zx_status_get_string(status));
    return zx::error(status);
  }

  // panel_type_ is now canonical.
  auto lcd_or_status = amlogic_display::Lcd::Create(
      &ac, self->panel_type_, self->panel_config_->dsi_on, self->panel_config_->dsi_off,
      fit::bind_member(self.get(), &DsiHost::SetSignalPower), self->dsiimpl_, self->lcd_gpio_,
      kBootloaderDisplayEnabled);
  if (!ac.check()) {
    DISP_ERROR("Unable to allocate an LCD object\n");
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  if (lcd_or_status.is_error()) {
    DISP_ERROR("Failed to create LCD object\n");
    return zx::error(lcd_or_status.error_value());
  }
  self->lcd_.reset(lcd_or_status.value());

  auto phy_or_status =
      amlogic_display::MipiPhy::Create(self->pdev_, self->dsiimpl_, kBootloaderDisplayEnabled);
  if (phy_or_status.is_error()) {
    DISP_ERROR("Failed to create PHY object\n");
    return zx::error(phy_or_status.error_value());
  }
  self->phy_ = std::move(phy_or_status.value());

  self->enabled_ = kBootloaderDisplayEnabled;

  return zx::ok(std::move(self));
}

zx_status_t DsiHost::LoadPowerTable(cpp20::span<const PowerOp> commands,
                                    fit::callback<zx_status_t()> power_on) {
  if (commands.size() == 0) {
    DISP_ERROR("No power commands to execute");
    return ZX_OK;
  }
  uint8_t read_value = 0;
  uint8_t wait_count = 0;
  zx_status_t status = ZX_OK;

  for (const auto op : commands) {
    DISP_TRACE("power_op %d index=%d value=%d sleep_ms=%d", op.op, op.index, op.value, op.sleep_ms);
    switch (op.op) {
      case kPowerOpExit:
        DISP_TRACE("power_exit");
        return ZX_OK;
      case kPowerOpGpio:
        DISP_TRACE("power_set_gpio pin #%d value=%d", op.index, op.value);
        if (op.index != 0) {
          DISP_ERROR("Unrecognized GPIO pin #%d, ignoring", op.index);
          break;
        }
        lcd_gpio_.Write(op.value);
        break;
      case kPowerOpSignal:
        DISP_TRACE("power_signal dsi_init");
        if ((status = power_on()) != ZX_OK) {
          return status;
        }
        break;
      case kPowerOpAwaitGpio:
        DISP_TRACE("power_await_gpio pin #%d value=%d timeout=%d msec", op.index, op.value,
                   op.sleep_ms);
        if (op.index != 0) {
          DISP_ERROR("Unrecognized GPIO pin #%d, ignoring", op.index);
          break;
        }
        lcd_gpio_.ConfigIn(0);
        for (wait_count = 0; wait_count < op.sleep_ms; wait_count++) {
          lcd_gpio_.Read(&read_value);
          if (read_value == op.value) {
            break;
          }
          zx::nanosleep(zx::deadline_after(zx::msec(1)));
        }
        if (wait_count == op.sleep_ms) {
          DISP_ERROR("Timed out waiting for GPIO value=%d", op.value);
        }
        break;
      default:
        DISP_ERROR("Unrecognized power op %d", op.op);
        break;
    }
    if (op.op != kPowerOpAwaitGpio && op.sleep_ms != 0) {
      DISP_TRACE("power_sleep %d msec", op.sleep_ms);
      zx::nanosleep(zx::deadline_after(zx::msec(op.sleep_ms)));
    }
  }
  return ZX_OK;
}

zx_status_t DsiHost::HostModeInit(const display_setting_t& disp_setting) {
  // Setup relevant TOP_CNTL register -- Undocumented --
  SET_BIT32(MIPI_DSI, MIPI_DSI_TOP_CNTL, SUPPORTED_DPI_FORMAT, TOP_CNTL_DPI_CLR_MODE_START,
            TOP_CNTL_DPI_CLR_MODE_BITS);
  SET_BIT32(MIPI_DSI, MIPI_DSI_TOP_CNTL, SUPPORTED_VENC_DATA_WIDTH, TOP_CNTL_IN_CLR_MODE_START,
            TOP_CNTL_IN_CLR_MODE_BITS);
  SET_BIT32(MIPI_DSI, MIPI_DSI_TOP_CNTL, 0, TOP_CNTL_CHROMA_SUBSAMPLE_START,
            TOP_CNTL_CHROMA_SUBSAMPLE_BITS);

  // setup dsi config
  dsi_config_t dsi_cfg;
  dsi_cfg.display_setting = disp_setting;
  dsi_cfg.video_mode_type = VIDEO_MODE_BURST;
  dsi_cfg.color_coding = COLOR_CODE_PACKED_24BIT_888;

  designware_config_t dw_cfg;
  dw_cfg.lp_escape_time = phy_->GetLowPowerEscaseTime();
  dw_cfg.lp_cmd_pkt_size = LPCMD_PKT_SIZE;
  dw_cfg.phy_timer_clkhs_to_lp = PHY_TMR_LPCLK_CLKHS_TO_LP;
  dw_cfg.phy_timer_clklp_to_hs = PHY_TMR_LPCLK_CLKLP_TO_HS;
  dw_cfg.phy_timer_hs_to_lp = PHY_TMR_HS_TO_LP;
  dw_cfg.phy_timer_lp_to_hs = PHY_TMR_LP_TO_HS;
  dw_cfg.auto_clklane = 1;
  dsi_cfg.vendor_config_buffer = reinterpret_cast<uint8_t*>(&dw_cfg);

  dsiimpl_.Config(&dsi_cfg);

  return ZX_OK;
}

void DsiHost::PhyEnable() {
  WRITE32_REG(HHI, HHI_MIPI_CNTL0,
              MIPI_CNTL0_CMN_REF_GEN_CTRL(0x29) | MIPI_CNTL0_VREF_SEL(VREF_SEL_VR) |
                  MIPI_CNTL0_LREF_SEL(LREF_SEL_L_ROUT) | MIPI_CNTL0_LBG_EN |
                  MIPI_CNTL0_VR_TRIM_CNTL(0x7) | MIPI_CNTL0_VR_GEN_FROM_LGB_EN);
  WRITE32_REG(HHI, HHI_MIPI_CNTL1, MIPI_CNTL1_DSI_VBG_EN | MIPI_CNTL1_CTL);
  WRITE32_REG(HHI, HHI_MIPI_CNTL2, MIPI_CNTL2_DEFAULT_VAL);  // 4 lane
}

void DsiHost::PhyDisable() {
  WRITE32_REG(HHI, HHI_MIPI_CNTL0, 0);
  WRITE32_REG(HHI, HHI_MIPI_CNTL1, 0);
  WRITE32_REG(HHI, HHI_MIPI_CNTL2, 0);
}

void DsiHost::SetSignalPower(bool on) {
  // These bits latch after vsync.
  if (on) {
    SET_BIT32(MIPI_DSI, MIPI_DSI_TOP_CNTL, 1, 2, 1);
    zx::nanosleep(zx::deadline_after(zx::msec(20)));
    SET_BIT32(MIPI_DSI, MIPI_DSI_TOP_CNTL, 0, 2, 1);
    zx::nanosleep(zx::deadline_after(zx::msec(20)));
  } else {
    SET_BIT32(MIPI_DSI, MIPI_DSI_TOP_CNTL, 0, 2, 1);
    zx::nanosleep(zx::deadline_after(zx::msec(20)));
  }
}

void DsiHost::Disable(const display_setting_t& disp_setting) {
  // turn host off only if it's been fully turned on
  if (!enabled_) {
    return;
  }

  // Place dsi in command mode first
  dsiimpl_.SetMode(DSI_MODE_COMMAND);
  fit::callback<zx_status_t()> power_off = [this]() {
    auto status = lcd_->Disable();
    if (status != ZX_OK) {
      return status;
    }
    PhyDisable();
    phy_->Shutdown();
    return ZX_OK;
  };

  auto status = LoadPowerTable(panel_config_->power_off, std::move(power_off));
  if (status != ZX_OK) {
    DISP_ERROR("Powering off a DSI display failed (%d)", status);
  }

  enabled_ = false;
}

zx_status_t DsiHost::Enable(const display_setting_t& disp_setting, uint32_t bitrate) {
  if (enabled_) {
    return ZX_OK;
  }

  fit::callback<zx_status_t()> power_on = [&]() {
    // Enable MIPI PHY
    PhyEnable();

    // Load Phy configuration
    zx_status_t status = phy_->PhyCfgLoad(bitrate);
    if (status != ZX_OK) {
      DISP_ERROR("Error during phy config calculations! %d\n", status);
      return status;
    }

    // Enable dwc mipi_dsi_host's clock
    SET_BIT32(MIPI_DSI, MIPI_DSI_TOP_CNTL, 0x3, 4, 2);
    // mipi_dsi_host's reset
    SET_BIT32(MIPI_DSI, MIPI_DSI_TOP_SW_RESET, 0xf, 0, 4);
    // Release mipi_dsi_host's reset
    SET_BIT32(MIPI_DSI, MIPI_DSI_TOP_SW_RESET, 0x0, 0, 4);
    // Enable dwc mipi_dsi_host's clock
    SET_BIT32(MIPI_DSI, MIPI_DSI_TOP_CLK_CNTL, 0x3, 0, 2);

    WRITE32_REG(MIPI_DSI, MIPI_DSI_TOP_MEM_PD, 0);
    zx::nanosleep(zx::deadline_after(zx::msec(10)));

    // Initialize host in command mode first
    dsiimpl_.SetMode(DSI_MODE_COMMAND);
    if ((status = HostModeInit(disp_setting)) != ZX_OK) {
      DISP_ERROR("Error during dsi host init! %d\n", status);
      return status;
    }

    // Initialize mipi dsi D-phy
    if ((status = phy_->Startup()) != ZX_OK) {
      DISP_ERROR("Error during MIPI D-PHY Initialization! %d\n", status);
      return status;
    }

    // Load LCD Init values while in command mode
    lcd_->Enable();

    // switch to video mode
    dsiimpl_.SetMode(DSI_MODE_VIDEO);
    return ZX_OK;
  };

  auto status = LoadPowerTable(panel_config_->power_on, std::move(power_on));
  if (status != ZX_OK) {
    DISP_ERROR("Failed to power on LCD (%d)", status);
    return status;
  }
  // Host is On and Active at this point
  enabled_ = true;
  return ZX_OK;
}

void DsiHost::Dump() {
  DISP_INFO("MIPI_DSI_TOP_SW_RESET = 0x%x\n", READ32_REG(MIPI_DSI, MIPI_DSI_TOP_SW_RESET));
  DISP_INFO("MIPI_DSI_TOP_CLK_CNTL = 0x%x\n", READ32_REG(MIPI_DSI, MIPI_DSI_TOP_CLK_CNTL));
  DISP_INFO("MIPI_DSI_TOP_CNTL = 0x%x\n", READ32_REG(MIPI_DSI, MIPI_DSI_TOP_CNTL));
  DISP_INFO("MIPI_DSI_TOP_SUSPEND_CNTL = 0x%x\n", READ32_REG(MIPI_DSI, MIPI_DSI_TOP_SUSPEND_CNTL));
  DISP_INFO("MIPI_DSI_TOP_SUSPEND_LINE = 0x%x\n", READ32_REG(MIPI_DSI, MIPI_DSI_TOP_SUSPEND_LINE));
  DISP_INFO("MIPI_DSI_TOP_SUSPEND_PIX = 0x%x\n", READ32_REG(MIPI_DSI, MIPI_DSI_TOP_SUSPEND_PIX));
  DISP_INFO("MIPI_DSI_TOP_MEAS_CNTL = 0x%x\n", READ32_REG(MIPI_DSI, MIPI_DSI_TOP_MEAS_CNTL));
  DISP_INFO("MIPI_DSI_TOP_STAT = 0x%x\n", READ32_REG(MIPI_DSI, MIPI_DSI_TOP_STAT));
  DISP_INFO("MIPI_DSI_TOP_MEAS_STAT_TE0 = 0x%x\n",
            READ32_REG(MIPI_DSI, MIPI_DSI_TOP_MEAS_STAT_TE0));
  DISP_INFO("MIPI_DSI_TOP_MEAS_STAT_TE1 = 0x%x\n",
            READ32_REG(MIPI_DSI, MIPI_DSI_TOP_MEAS_STAT_TE1));
  DISP_INFO("MIPI_DSI_TOP_MEAS_STAT_VS0 = 0x%x\n",
            READ32_REG(MIPI_DSI, MIPI_DSI_TOP_MEAS_STAT_VS0));
  DISP_INFO("MIPI_DSI_TOP_MEAS_STAT_VS1 = 0x%x\n",
            READ32_REG(MIPI_DSI, MIPI_DSI_TOP_MEAS_STAT_VS1));
  DISP_INFO("MIPI_DSI_TOP_INTR_CNTL_STAT = 0x%x\n",
            READ32_REG(MIPI_DSI, MIPI_DSI_TOP_INTR_CNTL_STAT));
  DISP_INFO("MIPI_DSI_TOP_MEM_PD = 0x%x\n", READ32_REG(MIPI_DSI, MIPI_DSI_TOP_MEM_PD));
}

}  // namespace amlogic_display
