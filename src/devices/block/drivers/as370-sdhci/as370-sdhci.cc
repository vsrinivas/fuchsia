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
  return SDHCI_QUIRK_NON_STANDARD_TUNING | SDHCI_QUIRK_STRIP_RESPONSE_CRC_PRESERVE_ORDER;
}

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

class Reg304 : public hwreg::RegisterBase<Reg304, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<Reg304>(0x304); }
  DEF_FIELD(28, 25, txslew_ctrl_n_dat);
  DEF_FIELD(24, 21, txslew_ctrl_p_dat);
  DEF_FIELD(20, 19, weakpull_en_dat);
  DEF_FIELD(18, 16, rxsel_dat);
  DEF_FIELD(12, 9, txslew_ctrl_n_cmd);
  DEF_FIELD(8, 5, txslew_ctrl_p_cmd);
  DEF_FIELD(4, 3, weakpull_en_cmd);
  DEF_FIELD(2, 0, rxsel_cmd);
};

class Reg308 : public hwreg::RegisterBase<Reg308, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<Reg308>(0x308); }
  DEF_FIELD(28, 25, txslew_ctrl_n_stb);
  DEF_FIELD(24, 21, txslew_ctrl_p_stb);
  DEF_FIELD(20, 19, weakpull_en_stb);
  DEF_FIELD(18, 16, rxsel_stb);
  DEF_FIELD(12, 9, txslew_ctrl_n_clk);
  DEF_FIELD(8, 5, txslew_ctrl_p_clk);
  DEF_FIELD(4, 3, weakpull_en_clk);
  DEF_FIELD(2, 0, rxsel_clk);
};

class Reg30c : public hwreg::RegisterBase<Reg30c, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<Reg30c>(0x30c); }
  DEF_FIELD(12, 9, txslew_ctrl_n_rst);
  DEF_FIELD(8, 5, txslew_ctrl_p_rst);
  DEF_FIELD(4, 3, weakpull_en_rst);
  DEF_FIELD(2, 0, rxsel_rst);
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
    //config PHY RXSEL
    Reg304::Get()
      .ReadFrom(&core_mmio_)
      .set_rxsel_cmd(1)
      .set_weakpull_en_cmd(WPE_PULLUP)
      .set_txslew_ctrl_p_cmd(TX_SLEW_P_0)
      .set_txslew_ctrl_n_cmd(TX_SLEW_N_3)
      .set_rxsel_dat(1)
      .set_weakpull_en_dat(WPE_PULLUP)
      .set_txslew_ctrl_p_dat(TX_SLEW_P_0)
      .set_txslew_ctrl_n_dat(TX_SLEW_N_3)
      .WriteTo(&core_mmio_);

    Reg308::Get()
      .ReadFrom(&core_mmio_)
      .set_rxsel_clk(0)
      .set_weakpull_en_clk(WPE_DISABLE)
      .set_txslew_ctrl_p_clk(TX_SLEW_P_0)
      .set_txslew_ctrl_n_clk(TX_SLEW_N_3)
      .set_rxsel_stb(1)
      .set_weakpull_en_stb(WPE_PULLDOWN)
      .set_txslew_ctrl_p_stb(TX_SLEW_P_0)
      .set_txslew_ctrl_n_stb(TX_SLEW_N_3)
      .WriteTo(&core_mmio_);

    Reg30c::Get()
      .ReadFrom(&core_mmio_)
      .set_rxsel_rst(1)
      .set_weakpull_en_rst(WPE_PULLUP)
      .set_txslew_ctrl_p_rst(TX_SLEW_P_0)
      .set_txslew_ctrl_n_rst(TX_SLEW_N_3)
      .WriteTo(&core_mmio_);

    // de-assert PHY reset
    PhyConfig::Get()
      .ReadFrom(&core_mmio_)
      .set_reset(1)
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
