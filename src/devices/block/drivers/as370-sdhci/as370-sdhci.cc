// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "as370-sdhci.h"

#include <lib/device-protocol/pdev.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <hwreg/bitfields.h>
#include <fbl/alloc_checker.h>

namespace sdhci {

zx_status_t As370Sdhci::Create(void* ctx, zx_device_t* parent) {
  ddk::PDev pdev(parent);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_PDEV not available\n", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  } else {
    pdev.ShowInfo();
  }

  std::optional<ddk::MmioBuffer> core_mmio;

  zx_status_t status = pdev.MapMmio(0, &core_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: MapMmio failed\n", __FILE__);
    return status;
  }
  zx::interrupt irq;
  if ((status = pdev.GetInterrupt(0, &irq)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to map interrupt\n", __FILE__);
    return status;
  }

  pdev_device_info_t device_info;
  status = pdev.GetDeviceInfo(&device_info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: GetDeviceInfo failed\n", __FILE__);
    return status;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<As370Sdhci> device(new (&ac)
                                         As370Sdhci(parent,
                                                    *std::move(core_mmio),
                                                    std::move(irq),
                                                    device_info.did));
  if (!ac.check()) {
    zxlogf(ERROR, "%s: As370Sdhci alloc failed\n", __FILE__);
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = device->Init()) != ZX_OK) {
    return status;
  }

  const char *device_name = "as370-sdhci";
  if (device_info.did == PDEV_DID_VS680_SDHCI0) {
    device_name = "vs680-sdhci";
  }

  if ((status = device->DdkAdd(device_name)) != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed\n", __FILE__);
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
  ddk::PDev pdev(parent());
  if (!pdev.is_valid()) {
    return ZX_ERR_NO_RESOURCES;
  }

  return pdev.GetBti(index, out_bti);
}

uint32_t As370Sdhci::SdhciGetBaseClock() { return 0; }

uint64_t As370Sdhci::SdhciGetQuirks() {
  uint64_t quirks = SDHCI_QUIRK_NO_DMA |
                    SDHCI_QUIRK_NON_STANDARD_TUNING |
                    SDHCI_QUIRK_STRIP_RESPONSE_CRC_PRESERVE_ORDER;

  if (did_ == PDEV_DID_VS680_SDHCI0) {
    quirks |= SDHCI_QUIRK_BUS_WIDTH_1;
  }

  return quirks;
}

#define RXSELOFF   0x0
#define SCHMITT1P8 0x1

#define WPE_DISABLE  0x0
#define WPE_PULLUP   0x1
#define WPE_PULLDOWN 0x2

#define TX_SLEW_P_0    0x0
#define TX_SLEW_N_3    0x3

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
  static auto Get(uint16_t vendor_ptr) {
    return hwreg::RegisterAddr<AtControl>(vendor_ptr + 0x40);
  }
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
  if (did_ == PDEV_DID_VS680_SDHCI0) {
    //config PHY_CNFG, general configuration
    //Dolphin_BG7_PHY_bring_up_sequence.xlsx
    //step 10
    PhyConfig::Get()
      .ReadFrom(&core_mmio_)
      .set_sp(8)
      .set_sn(8)
      .WriteTo(&core_mmio_);

    //Dolphin_BG7_PHY_bring_up_sequence.xlsx
    //step 11~15
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

    SdclkDlConfig::Get()
      .ReadFrom(&core_mmio_)
      .set_update_dc(1)
      .WriteTo(&core_mmio_);

    SdclkDlDc::Get()
      .ReadFrom(&core_mmio_)
      .set_cckdl_dc(30)
      .WriteTo(&core_mmio_);

    SdclkDlConfig::Get()
      .ReadFrom(&core_mmio_)
      .set_update_dc(0)
      .WriteTo(&core_mmio_);

    auto vptr = VendorPtr::Get().ReadFrom(&core_mmio_).reg_value();

    /* phy tuning setup */
    AtControl::Get(vptr)
      .ReadFrom(&core_mmio_)
      .set_tune_clk_stop_en(1)
      .set_post_change_dly(3)
      .set_pre_change_dly(3)
      .WriteTo(&core_mmio_);

    // de-assert PHY reset
    PhyConfig::Get()
      .ReadFrom(&core_mmio_)
      .set_reset(1)
      .WriteTo(&core_mmio_);

    EmmcControl::Get(vptr)
      .ReadFrom(&core_mmio_)
      .set_card_is_emmc(1)
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

ZIRCON_DRIVER_BEGIN(as370_sdhci, as370_sdhci_driver_ops, "zircon", "0.1", 4)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_SYNAPTICS),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AS370_SDHCI0),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_VS680_SDHCI0),
ZIRCON_DRIVER_END(as370_sdhci)
