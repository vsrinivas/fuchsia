// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "as370-sdhci.h"

#include <fuchsia/hardware/clock/cpp/banjo.h>
#include <fuchsia/hardware/i2c/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/i2c-channel.h>
#include <lib/device-protocol/pdev.h>

#include <memory>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <hwreg/bitfields.h>

#include "src/devices/block/drivers/as370-sdhci/as370-sdhci-bind.h"

namespace {

constexpr uint64_t kDmaBoundaryAlignment128M = 0x0800'0000;

constexpr uint64_t kVs680CoreClockFreqHz = 200'000'000;

constexpr uint32_t kPerifStickyResetNAddress = 0x688;
constexpr uint32_t kSdioPhyRstNBit = 5;

constexpr uint8_t kExpander2SdioOutputEnablePin = 0;
constexpr uint8_t kExpander3SdSlotPowerOnPin = 1;

constexpr uint8_t kIODirectionAddress = 0x3;
constexpr uint8_t kOutputStateAddress = 0x5;
constexpr uint8_t kOutputHighZAddress = 0x7;
constexpr uint8_t kPullEnableAddress = 0xb;

zx_status_t I2cModifyBit(ddk::I2cChannel& i2c, uint8_t reg, uint8_t set_mask, uint8_t clear_mask) {
  uint8_t reg_value = 0;
  zx_status_t status = i2c.ReadSync(reg, &reg_value, sizeof(reg_value));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to read from I2C register 0x%02x: %d", __FILE__, reg, status);
    return status;
  }

  reg_value = (reg_value | set_mask) & ~clear_mask;
  const uint8_t write_buf[] = {reg, reg_value};
  if ((status = i2c.WriteSync(write_buf, sizeof(write_buf))) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to write to I2C register 0x%02x: %d", __FILE__, reg, status);
    return status;
  }

  return ZX_OK;
}

zx_status_t SetExpanderGpioHigh(ddk::I2cChannel& expander, uint8_t bit) {
  const uint8_t mask = static_cast<uint8_t>(1 << bit);
  zx_status_t status;
  if ((status = I2cModifyBit(expander, kPullEnableAddress, 0, mask)) != ZX_OK ||
      (status = I2cModifyBit(expander, kIODirectionAddress, mask, 0)) != ZX_OK ||
      (status = I2cModifyBit(expander, kOutputStateAddress, mask, 0)) != ZX_OK ||
      (status = I2cModifyBit(expander, kOutputHighZAddress, 0, mask)) != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

}  // namespace

namespace sdhci {

zx_status_t As370Sdhci::Create(void* ctx, zx_device_t* parent) {
  ddk::PDev pdev;
  zx_status_t status;

  if (device_get_fragment_count(parent) > 0) {
    pdev = ddk::PDev::FromFragment(parent);

    // TODO(bradenkell): The GPIO expander code will likely be specific to the EVK board. Remove it
    //                   when we get new hardware.
    ddk::I2cChannel expander2(parent, "i2c-expander-2");
    if (!expander2.is_valid()) {
      zxlogf(ERROR, "%s: Could not get I2C fragment", __FILE__);
      return ZX_ERR_NO_RESOURCES;
    }

    ddk::I2cChannel expander3(parent, "i2c-expander-3");
    if (!expander3.is_valid()) {
      zxlogf(ERROR, "%s: Could not get I2C fragment", __FILE__);
      return ZX_ERR_NO_RESOURCES;
    }

    if ((status = SetExpanderGpioHigh(expander2, kExpander2SdioOutputEnablePin)) != ZX_OK ||
        (status = SetExpanderGpioHigh(expander3, kExpander3SdSlotPowerOnPin)) != ZX_OK) {
      return status;
    }

    // The SDIO core clock defaults to 100 MHz on VS680, even though the SDHCI capabilities register
    // says it is 200 MHz. Correct it so that the bus clock can be set properly.
    ddk::ClockProtocolClient clock(parent, "clock-sd-0");
    if (clock.is_valid() && (status = clock.SetRate(kVs680CoreClockFreqHz)) != ZX_OK) {
      zxlogf(WARNING, "%s: Failed to set core clock frequency: %d", __FILE__, status);
    }
  } else {
    pdev = ddk::PDev(parent);
  }

  if (!pdev.is_valid()) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_PDEV not available", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  } else {
    pdev.ShowInfo();
  }

  std::optional<ddk::MmioBuffer> core_mmio;

  if ((status = pdev.MapMmio(0, &core_mmio)) != ZX_OK) {
    zxlogf(ERROR, "%s: MapMmio failed", __FILE__);
    return status;
  }
  zx::interrupt irq;
  if ((status = pdev.GetInterrupt(0, &irq)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to map interrupt", __FILE__);
    return status;
  }

  pdev_device_info_t device_info;
  status = pdev.GetDeviceInfo(&device_info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: GetDeviceInfo failed", __FILE__);
    return status;
  }

  zx::bti bti;
  if ((status = pdev.GetBti(0, &bti)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get BTI: %d", __FILE__, status);
    return status;
  }

  std::optional<ddk::MmioBuffer> reset_mmio;
  if (device_info.did == PDEV_DID_VS680_SDHCI1 &&
      (status = pdev.MapMmio(1, &reset_mmio)) == ZX_OK) {
    // Set the (active low) reset bit for the SDIO phy on VS680.
    reset_mmio->SetBit<uint32_t>(kSdioPhyRstNBit, kPerifStickyResetNAddress);
  }

  fbl::AllocChecker ac;
  std::unique_ptr<As370Sdhci> device(new (&ac) As370Sdhci(
      parent, *std::move(core_mmio), std::move(irq), device_info.did, std::move(bti)));
  if (!ac.check()) {
    zxlogf(ERROR, "%s: As370Sdhci alloc failed", __FILE__);
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = device->Init()) != ZX_OK) {
    return status;
  }

  const char* device_name = "as370-sdhci";
  if (device_info.did == PDEV_DID_VS680_SDHCI0) {
    device_name = "vs680-sdhci";
  }

  if ((status = device->DdkAdd(device_name)) != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed", __FILE__);
    return status;
  }

  __UNUSED auto* dummy = device.release();

  return ZX_OK;
}

zx_status_t As370Sdhci::Init() { return ZX_OK; }

zx_status_t As370Sdhci::SdhciGetInterrupt(zx::interrupt* out_irq) {
  out_irq->reset(irq_.release());
  return ZX_OK;
}

zx_status_t As370Sdhci::SdhciGetMmio(zx::vmo* out_mmio, zx_off_t* out_offset) {
  core_mmio_.get_vmo()->duplicate(ZX_RIGHT_SAME_RIGHTS, out_mmio);
  *out_offset = core_mmio_.get_offset();
  return ZX_OK;
}

zx_status_t As370Sdhci::SdhciGetBti(uint32_t index, zx::bti* out_bti) {
  *out_bti = std::move(bti_);
  return ZX_OK;
}

// TODO(bradenkell): The VS680 SDIO base clock seems to be different than what the controller
//                   expects, as the bus frequency is half of what it should be.
uint32_t As370Sdhci::SdhciGetBaseClock() { return 0; }

uint64_t As370Sdhci::SdhciGetQuirks(uint64_t* out_dma_boundary_alignment) {
  *out_dma_boundary_alignment = kDmaBoundaryAlignment128M;
  uint64_t quirks = SDHCI_QUIRK_NON_STANDARD_TUNING |
                    SDHCI_QUIRK_STRIP_RESPONSE_CRC_PRESERVE_ORDER |
                    SDHCI_QUIRK_USE_DMA_BOUNDARY_ALIGNMENT;

  // Tuning currently doesn't work on AS370/VS680 so HS200/HS400/SDR104 can't be used. VS680 has
  // eMMC for which the next fallback is HSDDR, however this also doesn't work on the board we have.
  // Enable the following quirk so that HS is used instead of HSDDR.
  if (did_ == PDEV_DID_VS680_SDHCI0) {
    quirks |= SDHCI_QUIRK_NO_DDR;
  }

  return quirks;
}

#define RXSELOFF 0x0
#define SCHMITT1P8 0x1

#define WPE_DISABLE 0x0
#define WPE_PULLUP 0x1
#define WPE_PULLDOWN 0x2

#define TX_SLEW_P_0 0x0
#define TX_SLEW_N_3 0x3

class PhyConfig : public hwreg::RegisterBase<PhyConfig, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<PhyConfig>(0x300); }
  DEF_FIELD(23, 20, sn);
  DEF_FIELD(19, 16, sp);
  DEF_BIT(0, reset);
};

class PadConfig : public hwreg::RegisterBase<PadConfig, uint16_t> {
 public:
  DEF_FIELD(12, 9, txslew_ctrl_n);
  DEF_FIELD(8, 5, txslew_ctrl_p);
  DEF_FIELD(4, 3, weakpull_en);
  DEF_FIELD(2, 0, rxsel);
};

class CmdPadConfig : public PadConfig {
 public:
  static auto Get() { return hwreg::RegisterAddr<PadConfig>(0x304); }
};

class DatPadConfig : public PadConfig {
 public:
  static auto Get() { return hwreg::RegisterAddr<PadConfig>(0x306); }
};

class ClkPadConfig : public PadConfig {
 public:
  static auto Get() { return hwreg::RegisterAddr<PadConfig>(0x308); }
};

class StbPadConfig : public PadConfig {
 public:
  static auto Get() { return hwreg::RegisterAddr<PadConfig>(0x30a); }
};

class RstPadConfig : public PadConfig {
 public:
  static auto Get() { return hwreg::RegisterAddr<PadConfig>(0x30c); }
};

class CommDlConfig : public hwreg::RegisterBase<CommDlConfig, uint8_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<CommDlConfig>(0x31c); }
  DEF_BIT(1, dlout_en);
  DEF_BIT(0, dlstep_sel);
};

class SdclkDlConfig : public hwreg::RegisterBase<SdclkDlConfig, uint8_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<SdclkDlConfig>(0x31d); }
  DEF_BIT(4, update_dc);
  DEF_FIELD(3, 2, inpsel_cnfg);
  DEF_BIT(1, bypass_en);
  DEF_BIT(0, extdly_en);
};

class SdclkDlDc : public hwreg::RegisterBase<SdclkDlDc, uint8_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<SdclkDlDc>(0x31e); }
  DEF_FIELD(7, 0, cckdl_dc);
};

class SmplDlConfig : public hwreg::RegisterBase<SmplDlConfig, uint8_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<SmplDlConfig>(0x320); }
  DEF_BIT(4, sinpsel_override);
  DEF_FIELD(3, 2, sinpsel_cnfg);
  DEF_BIT(1, sbypass_en);
  DEF_BIT(0, sextdly_en);
};

class AtDlConfig : public hwreg::RegisterBase<AtDlConfig, uint8_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AtDlConfig>(0x321); }
  DEF_FIELD(3, 2, ainpsel_cnfg);
  DEF_BIT(1, abypass_en);
  DEF_BIT(0, aextdly_en);
};

class VendorPtr : public hwreg::RegisterBase<VendorPtr, uint16_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<VendorPtr>(0xe8); }
};

class EmmcControl : public hwreg::RegisterBase<EmmcControl, uint32_t> {
 public:
  static auto Get(uint16_t vendor_ptr) {
    return hwreg::RegisterAddr<EmmcControl>(vendor_ptr + 0x2c);
  }
  DEF_BIT(0, card_is_emmc);
};

class AtControl : public hwreg::RegisterBase<AtControl, uint32_t> {
 public:
  static auto Get(uint16_t vendor_ptr) { return hwreg::RegisterAddr<AtControl>(vendor_ptr + 0x40); }
  DEF_FIELD(20, 19, post_change_dly);
  DEF_FIELD(18, 17, pre_change_dly);
  DEF_BIT(16, tune_clk_stop_en);
  DEF_BIT(4, sw_tune_en);
  DEF_BIT(3, rpt_tune_err);
  DEF_BIT(2, swin_th_en);
  DEF_BIT(1, ci_sel);
  DEF_BIT(0, at_en);
};

void As370Sdhci::SdhciHwReset() {
  if (did_ == PDEV_DID_VS680_SDHCI0 || did_ == PDEV_DID_VS680_SDHCI1) {
    // config PHY_CNFG, general configuration
    // Dolphin_BG7_PHY_bring_up_sequence.xlsx
    // step 10
    PhyConfig::Get().ReadFrom(&core_mmio_).set_sp(8).set_sn(8).WriteTo(&core_mmio_);

    // Dolphin_BG7_PHY_bring_up_sequence.xlsx
    // step 11~15
    CmdPadConfig::Get()
        .ReadFrom(&core_mmio_)
        .set_rxsel(SCHMITT1P8)
        .set_weakpull_en(WPE_PULLUP)
        .set_txslew_ctrl_p(TX_SLEW_P_0)
        .set_txslew_ctrl_n(TX_SLEW_N_3)
        .WriteTo(&core_mmio_);

    DatPadConfig::Get()
        .ReadFrom(&core_mmio_)
        .set_rxsel(SCHMITT1P8)
        .set_weakpull_en(WPE_PULLUP)
        .set_txslew_ctrl_p(TX_SLEW_P_0)
        .set_txslew_ctrl_n(TX_SLEW_N_3)
        .WriteTo(&core_mmio_);

    ClkPadConfig::Get()
        .ReadFrom(&core_mmio_)
        .set_rxsel(RXSELOFF)
        .set_weakpull_en(WPE_DISABLE)
        .set_txslew_ctrl_p(TX_SLEW_P_0)
        .set_txslew_ctrl_n(TX_SLEW_N_3)
        .WriteTo(&core_mmio_);

    StbPadConfig::Get()
        .ReadFrom(&core_mmio_)
        .set_rxsel(SCHMITT1P8)
        .set_weakpull_en(WPE_PULLDOWN)
        .set_txslew_ctrl_p(TX_SLEW_P_0)
        .set_txslew_ctrl_n(TX_SLEW_N_3)
        .WriteTo(&core_mmio_);

    RstPadConfig::Get()
        .ReadFrom(&core_mmio_)
        .set_rxsel(SCHMITT1P8)
        .set_weakpull_en(WPE_PULLUP)
        .set_txslew_ctrl_p(TX_SLEW_P_0)
        .set_txslew_ctrl_n(TX_SLEW_N_3)
        .WriteTo(&core_mmio_);

    /* phy delay line setup */
    CommDlConfig::Get()
        .ReadFrom(&core_mmio_)
        .set_dlstep_sel(0)
        .set_dlout_en(0)
        .WriteTo(&core_mmio_);

    SdclkDlConfig::Get()
        .ReadFrom(&core_mmio_)
        .set_extdly_en(0)
        .set_bypass_en(0)
        .set_inpsel_cnfg(0)
        .set_update_dc(0)
        .WriteTo(&core_mmio_);

    SmplDlConfig::Get()
        .ReadFrom(&core_mmio_)
        .set_sextdly_en(0)
        .set_sbypass_en(0)
        .set_sinpsel_override(0)
        .set_sinpsel_cnfg(3)
        .WriteTo(&core_mmio_);

    AtDlConfig::Get()
        .ReadFrom(&core_mmio_)
        .set_aextdly_en(0)
        .set_abypass_en(0)
        .set_ainpsel_cnfg(3)
        .WriteTo(&core_mmio_);

    SdclkDlConfig::Get().ReadFrom(&core_mmio_).set_update_dc(1).WriteTo(&core_mmio_);

    SdclkDlDc::Get().ReadFrom(&core_mmio_).set_cckdl_dc(0x7f).WriteTo(&core_mmio_);

    SdclkDlConfig::Get().ReadFrom(&core_mmio_).set_update_dc(0).WriteTo(&core_mmio_);

    auto vptr = VendorPtr::Get().ReadFrom(&core_mmio_).reg_value();

    /* phy tuning setup */
    AtControl::Get(vptr)
        .ReadFrom(&core_mmio_)
        .set_tune_clk_stop_en(1)
        .set_post_change_dly(3)
        .set_pre_change_dly(3)
        .WriteTo(&core_mmio_);

    // de-assert PHY reset
    PhyConfig::Get().ReadFrom(&core_mmio_).set_reset(1).WriteTo(&core_mmio_);

    EmmcControl::Get(vptr)
        .ReadFrom(&core_mmio_)
        .set_card_is_emmc(did_ == PDEV_DID_VS680_SDHCI0 ? 1 : 0)
        .WriteTo(&core_mmio_);
  }
}

}  // namespace sdhci

static constexpr zx_driver_ops_t as370_sdhci_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = sdhci::As370Sdhci::Create;
  return ops;
}();

ZIRCON_DRIVER(as370_sdhci, as370_sdhci_driver_ops, "zircon", "0.1");
