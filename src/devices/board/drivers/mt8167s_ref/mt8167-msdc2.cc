// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/platform-defs.h>
#include <lib/mmio/mmio.h>
#include <limits.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <fbl/algorithm.h>
#include <hwreg/bitfields.h>
#include <soc/mt8167/mt8167-hw.h>
#include <soc/mt8167/mt8167-sdmmc.h>

#include "mt8167.h"
#include "src/devices/board/drivers/mt8167s_ref/mt8167_bind.h"

namespace {

constexpr uint16_t kPullUp = 0;
constexpr uint16_t kPullDown = 1;

constexpr uint16_t kPull10k = 1;
constexpr uint16_t kPull50k = 2;

constexpr uint16_t kDriveStrength6mA = 2;
constexpr uint16_t kDriveStrength8mA = 3;

constexpr uintptr_t kIocfgBaseAligned =
    fbl::round_down<uintptr_t, uintptr_t>(MT8167_IOCFG_BASE, PAGE_SIZE);
constexpr size_t kIocfgOffset = MT8167_IOCFG_BASE - kIocfgBaseAligned;
constexpr size_t kIocfgSizeAligned =
    fbl::round_up<size_t, size_t>(kIocfgOffset + MT8167_IOCFG_SIZE, PAGE_SIZE);

constexpr uintptr_t kGpioBaseAligned =
    fbl::round_down<uintptr_t, uintptr_t>(MT8167_GPIO_BASE, PAGE_SIZE);
constexpr size_t kGpioOffset = MT8167_GPIO_BASE - kGpioBaseAligned;
constexpr size_t kGpioSizeAligned =
    fbl::round_up<size_t, size_t>(kGpioOffset + MT8167_GPIO_SIZE, PAGE_SIZE);

constexpr uint32_t kFifoDepth = 128;
constexpr uint32_t kSrcClkFreq = 206'000'000;

}  // namespace

namespace board_mt8167 {

class PuPdCtrl4 : public hwreg::RegisterBase<PuPdCtrl4, uint16_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<PuPdCtrl4>(kIocfgOffset + 0x540); }

  DEF_BIT(14, msdc2_dat2_pupd);
  DEF_FIELD(13, 12, msdc2_dat2_pull);

  DEF_BIT(10, msdc2_dat1_pupd);
  DEF_FIELD(9, 8, msdc2_dat1_pull);

  DEF_BIT(6, msdc2_dat0_pupd);
  DEF_FIELD(5, 4, msdc2_dat0_pull);
};

class PuPdCtrl5 : public hwreg::RegisterBase<PuPdCtrl5, uint16_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<PuPdCtrl5>(kIocfgOffset + 0x550); }

  DEF_BIT(10, msdc2_cmd_pupd);
  DEF_FIELD(9, 8, msdc2_cmd_pull);

  DEF_BIT(6, msdc2_clk_pupd);
  DEF_FIELD(5, 4, msdc2_clk_pull);

  DEF_BIT(2, msdc2_dat3_pupd);
  DEF_FIELD(1, 0, msdc2_dat3_pull);
};

constexpr uint16_t kGpioModeMsdc2 = 1;

class GpioModeE : public hwreg::RegisterBase<GpioModeE, uint16_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<GpioModeE>(kGpioOffset + 0x3d0); }

  DEF_FIELD(14, 12, gpio69_mode);
  DEF_FIELD(11, 9, gpio68_mode);
};

class GpioModeF : public hwreg::RegisterBase<GpioModeF, uint16_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<GpioModeF>(kGpioOffset + 0x3e0); }

  DEF_FIELD(11, 9, gpio73_mode);
  DEF_FIELD(8, 6, gpio72_mode);
  DEF_FIELD(5, 3, gpio71_mode);
  DEF_FIELD(2, 0, gpio70_mode);
};

class Smt3En : public hwreg::RegisterBase<Smt3En, uint16_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<Smt3En>(kIocfgOffset + 0x130); }
  DEF_BIT(6, msdc2_dat3_smt_en);
  DEF_BIT(5, msdc2_dat2_smt_en);
  DEF_BIT(4, msdc2_dat1_smt_en);
  DEF_BIT(3, msdc2_dat0_smt_en);
  DEF_BIT(2, msdc2_cmd_smt_en);
  DEF_BIT(1, msdc2_clk_smt_en);
};

class DrvMode4 : public hwreg::RegisterBase<DrvMode4, uint16_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<DrvMode4>(kIocfgOffset + 0x440); }

  DEF_FIELD(14, 12, msdc2_cmd_drive_strength);
};

class DrvMode5 : public hwreg::RegisterBase<DrvMode5, uint16_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<DrvMode5>(kIocfgOffset + 0x450); }

  DEF_FIELD(6, 4, msdc2_dat_drive_strength);
  DEF_FIELD(2, 0, msdc2_clk_drive_strength);
};

zx_status_t Mt8167::Msdc2Init() {
  static const pbus_mmio_t msdc2_mmios[] = {{
      .base = MT8167_MSDC2_BASE,
      .length = MT8167_MSDC2_SIZE,
  }};

  static const pbus_bti_t msdc2_btis[] = {{
      .iommu_index = 0,
      .bti_id = BTI_MSDC2,
  }};

  static const MtkSdmmcConfig msdc2_config = {
      .fifo_depth = kFifoDepth, .src_clk_freq = kSrcClkFreq, .is_sdio = true};

  static const pbus_metadata_t msdc2_metadata[] = {
      {.type = DEVICE_METADATA_PRIVATE,
       .data_buffer = reinterpret_cast<const uint8_t*>(&msdc2_config),
       .data_size = sizeof(msdc2_config)},
  };

  static const pbus_irq_t msdc2_irqs[] = {
      {.irq = MT8167_IRQ_MSDC2, .mode = ZX_INTERRUPT_MODE_EDGE_HIGH}};

  pbus_dev_t msdc2_dev = {};
  msdc2_dev.name = "sdio";
  msdc2_dev.vid = PDEV_VID_MEDIATEK;
  msdc2_dev.did = PDEV_DID_MEDIATEK_MSDC2;
  msdc2_dev.mmio_list = msdc2_mmios;
  msdc2_dev.mmio_count = countof(msdc2_mmios);
  msdc2_dev.bti_list = msdc2_btis;
  msdc2_dev.bti_count = countof(msdc2_btis);
  msdc2_dev.metadata_list = msdc2_metadata;
  msdc2_dev.metadata_count = countof(msdc2_metadata);
  msdc2_dev.irq_list = msdc2_irqs;
  msdc2_dev.irq_count = countof(msdc2_irqs);

  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx::unowned_resource root_resource(get_root_resource());
  std::optional<ddk::MmioBuffer> iocfg_mmio;
  zx_status_t status = ddk::MmioBuffer::Create(kIocfgBaseAligned, kIocfgSizeAligned, *root_resource,
                                               ZX_CACHE_POLICY_UNCACHED_DEVICE, &iocfg_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to set MSDC2 GPIOs: %d", __FUNCTION__, status);
    return status;
  }

  // MSDC2 pins are not configured by the bootloader. Set the clk pin to 50k pull-down, all others
  // to 10k pull-up to match the device tree settings.
  PuPdCtrl4::Get()
      .ReadFrom(&(*iocfg_mmio))
      .set_msdc2_dat2_pupd(kPullUp)
      .set_msdc2_dat2_pull(kPull10k)
      .set_msdc2_dat1_pupd(kPullUp)
      .set_msdc2_dat1_pull(kPull10k)
      .set_msdc2_dat0_pupd(kPullUp)
      .set_msdc2_dat0_pull(kPull10k)
      .WriteTo(&(*iocfg_mmio));

  PuPdCtrl5::Get()
      .ReadFrom(&(*iocfg_mmio))
      .set_msdc2_cmd_pupd(kPullUp)
      .set_msdc2_cmd_pull(kPull10k)
      .set_msdc2_clk_pupd(kPullDown)
      .set_msdc2_clk_pull(kPull50k)
      .set_msdc2_dat3_pupd(kPullUp)
      .set_msdc2_dat3_pull(kPull10k)
      .WriteTo(&(*iocfg_mmio));

  Smt3En::Get()
      .ReadFrom(&(*iocfg_mmio))
      .set_msdc2_dat3_smt_en(1)
      .set_msdc2_dat2_smt_en(1)
      .set_msdc2_dat1_smt_en(1)
      .set_msdc2_dat0_smt_en(1)
      .set_msdc2_cmd_smt_en(1)
      .set_msdc2_clk_smt_en(1)
      .WriteTo(&(*iocfg_mmio));

  DrvMode4::Get()
      .ReadFrom(&(*iocfg_mmio))
      .set_msdc2_cmd_drive_strength(kDriveStrength6mA)
      .WriteTo(&(*iocfg_mmio));
  DrvMode5::Get()
      .ReadFrom(&(*iocfg_mmio))
      .set_msdc2_clk_drive_strength(kDriveStrength8mA)
      .set_msdc2_dat_drive_strength(kDriveStrength6mA)
      .WriteTo(&(*iocfg_mmio));

  std::optional<ddk::MmioBuffer> gpio_mmio;
  status = ddk::MmioBuffer::Create(kGpioBaseAligned, kGpioSizeAligned, *root_resource,
                                   ZX_CACHE_POLICY_UNCACHED_DEVICE, &gpio_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to set MSDC2 GPIOs: %d", __FUNCTION__, status);
    return status;
  }

  GpioModeE::Get()
      .ReadFrom(&(*gpio_mmio))
      .set_gpio69_mode(kGpioModeMsdc2)
      .set_gpio68_mode(kGpioModeMsdc2)
      .WriteTo(&(*gpio_mmio));

  GpioModeF::Get()
      .ReadFrom(&(*gpio_mmio))
      .set_gpio73_mode(kGpioModeMsdc2)
      .set_gpio72_mode(kGpioModeMsdc2)
      .set_gpio71_mode(kGpioModeMsdc2)
      .set_gpio70_mode(kGpioModeMsdc2)
      .WriteTo(&(*gpio_mmio));

  static constexpr zx_bind_inst_t root_match[] = {
      BI_MATCH(),
  };

  static constexpr zx_bind_inst_t reset_gpio_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
      BI_MATCH_IF(EQ, BIND_GPIO_PIN, MT8167_GPIO_MT7668_PMU_EN),
  };
  static constexpr zx_bind_inst_t power_en_gpio_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
      BI_MATCH_IF(EQ, BIND_GPIO_PIN, MT8167_CLEO_GPIO_HUB_PWR_EN),
  };

  static const device_fragment_part_t reset_gpio_fragment[] = {
      {std::size(root_match), root_match},
      {std::size(reset_gpio_match), reset_gpio_match},
  };
  static const device_fragment_part_t power_en_gpio_fragment[] = {
      {std::size(root_match), root_match},
      {std::size(power_en_gpio_match), power_en_gpio_match},
  };

  static const device_fragment_t ref_fragments[] = {
      {"gpio-reset", std::size(reset_gpio_fragment), reset_gpio_fragment},
  };
  static const device_fragment_t cleo_fragments[] = {
      {"gpio-reset", std::size(reset_gpio_fragment), reset_gpio_fragment},
      {"gpio-power-enable", std::size(power_en_gpio_fragment), power_en_gpio_fragment},
  };

  if (board_info_.vid == PDEV_VID_GOOGLE && board_info_.pid == PDEV_PID_CLEO) {
    status = pbus_.CompositeDeviceAdd(&msdc2_dev, reinterpret_cast<uint64_t>(cleo_fragments),
                                      std::size(cleo_fragments), UINT32_MAX);
  } else {
    status = pbus_.CompositeDeviceAdd(&msdc2_dev, reinterpret_cast<uint64_t>(ref_fragments),
                                      std::size(ref_fragments), UINT32_MAX);
  }

  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd MSDC2 failed: %d", __FUNCTION__, status);
  }

  return status;
}

}  // namespace board_mt8167
