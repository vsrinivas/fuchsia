// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/align.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/metadata/init-step.h>
#include <ddk/platform-defs.h>
#include <fbl/algorithm.h>
#include <hw/reg.h>
#include <hwreg/bitfields.h>
#include <soc/aml-common/aml-sd-emmc.h>
#include <soc/aml-s905d3/s905d3-gpio.h>
#include <soc/aml-s905d3/s905d3-hw.h>
#include <wifi/wifi-config.h>

#include "nelson-gpios.h"
#include "nelson.h"

namespace nelson {

namespace {

constexpr uint32_t kGpioBase = fbl::round_down<uint32_t, uint32_t>(S905D3_GPIO_BASE, PAGE_SIZE);
constexpr uint32_t kGpioBaseOffset = S905D3_GPIO_BASE - kGpioBase;

class PadDsReg2A : public hwreg::RegisterBase<PadDsReg2A, uint32_t> {
 public:
  static constexpr uint32_t kDriveStrengthMax = 3;

  static auto Get() { return hwreg::RegisterAddr<PadDsReg2A>((0xd2 * 4) + kGpioBaseOffset); }

  DEF_FIELD(1, 0, gpiox_0_select);
  DEF_FIELD(3, 2, gpiox_1_select);
  DEF_FIELD(5, 4, gpiox_2_select);
  DEF_FIELD(7, 6, gpiox_3_select);
  DEF_FIELD(9, 8, gpiox_4_select);
  DEF_FIELD(11, 10, gpiox_5_select);
};

}  // namespace

static const pbus_boot_metadata_t wifi_boot_metadata[] = {
    {
        .zbi_type = DEVICE_METADATA_MAC_ADDRESS,
        .zbi_extra = MACADDR_WIFI,
    },
};

static const pbus_mmio_t sd_emmc_mmios[] = {
    {
        .base = S905D3_EMMC_A_SDIO_BASE,
        .length = S905D3_EMMC_A_SDIO_LENGTH,
    },
    {
        .base = S905D3_GPIO_BASE,
        .length = S905D3_GPIO_LENGTH,
    },
    {
        .base = S905D3_HIU_BASE,
        .length = S905D3_HIU_LENGTH,
    },
};

static const pbus_irq_t sd_emmc_irqs[] = {
    {
        .irq = S905D3_EMMC_A_SDIO_IRQ,
        .mode = 0,
    },
};

static const pbus_bti_t sd_emmc_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_SDIO,
    },
};

static aml_sd_emmc_config_t config = {
    .supports_dma = true,
    .min_freq = 400'000,
    .max_freq = 208'000'000,
    .version_3 = true,
    .prefs = 0,
};

constexpr wifi_config_t wifi_config = {
    .oob_irq_mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    .iovar_table =
        {
            {IOVAR_STR_TYPE, {"ampdu_ba_wsize"}, 32},
            {IOVAR_STR_TYPE, {"stbc_tx"}, 1},
            {IOVAR_STR_TYPE, {"stbc_rx"}, 1},
            {IOVAR_CMD_TYPE, {{BRCMF_C_SET_PM}}, 0},
            {IOVAR_CMD_TYPE, {{BRCMF_C_SET_FAKEFRAG}}, 1},
            {IOVAR_LIST_END_TYPE, {{0}}, 0},
        },
    .cc_table =
        {
            {"WW", 1},   {"AU", 923}, {"CA", 901}, {"US", 843}, {"GB", 889}, {"BE", 889},
            {"BG", 889}, {"CZ", 889}, {"DK", 889}, {"DE", 889}, {"EE", 889}, {"IE", 889},
            {"GR", 889}, {"ES", 889}, {"FR", 889}, {"HR", 889}, {"IT", 889}, {"CY", 889},
            {"LV", 889}, {"LT", 889}, {"LU", 889}, {"HU", 889}, {"MT", 889}, {"NL", 889},
            {"AT", 889}, {"PL", 889}, {"PT", 889}, {"RO", 889}, {"SI", 889}, {"SK", 889},
            {"FI", 889}, {"SE", 889}, {"EL", 889}, {"IS", 889}, {"LI", 889}, {"TR", 889},
            {"CH", 889}, {"NO", 889}, {"JP", 2},   {"KR", 2},   {"TW", 2},   {"IN", 2},
            {"SG", 2},   {"MX", 2},   {"NZ", 2},   {"", 0},
        },
};

static const pbus_metadata_t sd_emmc_metadata[] = {
    {
        .type = DEVICE_METADATA_EMMC_CONFIG,
        .data_buffer = &config,
        .data_size = sizeof(config),
    },
    {
        .type = DEVICE_METADATA_WIFI_CONFIG,
        .data_buffer = &wifi_config,
        .data_size = sizeof(wifi_config),
    },
};

static const pbus_dev_t sd_emmc_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-sdio";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_AMLOGIC_SD_EMMC_A;
  dev.mmio_list = sd_emmc_mmios;
  dev.mmio_count = countof(sd_emmc_mmios);
  dev.irq_list = sd_emmc_irqs;
  dev.irq_count = countof(sd_emmc_irqs);
  dev.bti_list = sd_emmc_btis;
  dev.bti_count = countof(sd_emmc_btis);
  dev.metadata_list = sd_emmc_metadata;
  dev.metadata_count = countof(sd_emmc_metadata);
  dev.boot_metadata_list = wifi_boot_metadata;
  dev.boot_metadata_count = countof(wifi_boot_metadata);
  return dev;
}();

// Composite binding rules for wifi driver.
static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static const zx_bind_inst_t sdio_fn1_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_SDIO), BI_ABORT_IF(NE, BIND_SDIO_VID, 0x02d0),
    BI_ABORT_IF(NE, BIND_SDIO_FUNCTION, 1),           BI_MATCH_IF(EQ, BIND_SDIO_PID, 0x4345),
    BI_MATCH_IF(EQ, BIND_SDIO_PID, 0x4359),
};
static const zx_bind_inst_t sdio_fn2_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_SDIO), BI_ABORT_IF(NE, BIND_SDIO_VID, 0x02d0),
    BI_ABORT_IF(NE, BIND_SDIO_FUNCTION, 2),           BI_MATCH_IF(EQ, BIND_SDIO_PID, 0x4345),
    BI_MATCH_IF(EQ, BIND_SDIO_PID, 0x4359),
};
static const zx_bind_inst_t oob_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, S905D3_WIFI_SDIO_WAKE_HOST),
};
static const device_fragment_part_t sdio_fn1_fragment[] = {
    {countof(root_match), root_match},
    {countof(sdio_fn1_match), sdio_fn1_match},
};
static const device_fragment_part_t sdio_fn2_fragment[] = {
    {countof(root_match), root_match},
    {countof(sdio_fn2_match), sdio_fn2_match},
};
static const device_fragment_part_t oob_gpio_fragment[] = {
    {countof(root_match), root_match},
    {countof(oob_gpio_match), oob_gpio_match},
};
static const device_fragment_t wifi_composite[] = {
    {countof(sdio_fn1_fragment), sdio_fn1_fragment},
    {countof(sdio_fn2_fragment), sdio_fn2_fragment},
    {countof(oob_gpio_fragment), oob_gpio_fragment},
};

// Composite binding rules for SDIO.
static const zx_bind_inst_t wifi_pwren_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_WIFI_REG_ON),
};
constexpr zx_bind_inst_t pwm_e_match[] = {
    BI_MATCH_IF(EQ, BIND_INIT_STEP, BIND_INIT_STEP_PWM),
};
static const device_fragment_part_t wifi_pwren_gpio_fragment[] = {
    {countof(root_match), root_match},
    {countof(wifi_pwren_gpio_match), wifi_pwren_gpio_match},
};
constexpr device_fragment_part_t pwm_e_fragment[] = {
    {countof(root_match), root_match},
    {countof(pwm_e_match), pwm_e_match},
};
static const device_fragment_t sdio_fragments[] = {
    {countof(wifi_pwren_gpio_fragment), wifi_pwren_gpio_fragment},
    {countof(pwm_e_fragment), pwm_e_fragment},
};

zx_status_t Nelson::SdEmmcConfigurePortB() {
  gpio_impl_.SetAltFunction(S905D3_WIFI_SDIO_D0, S905D3_WIFI_SDIO_D0_FN);
  gpio_impl_.SetAltFunction(S905D3_WIFI_SDIO_D1, S905D3_WIFI_SDIO_D1_FN);
  gpio_impl_.SetAltFunction(S905D3_WIFI_SDIO_D2, S905D3_WIFI_SDIO_D2_FN);
  gpio_impl_.SetAltFunction(S905D3_WIFI_SDIO_D3, S905D3_WIFI_SDIO_D3_FN);
  gpio_impl_.SetAltFunction(S905D3_WIFI_SDIO_CLK, S905D3_WIFI_SDIO_CLK_FN);
  gpio_impl_.SetAltFunction(S905D3_WIFI_SDIO_CMD, S905D3_WIFI_SDIO_CMD_FN);
  gpio_impl_.SetAltFunction(S905D3_WIFI_SDIO_WAKE_HOST, 0);

  zx_status_t status;
  std::optional<ddk::MmioBuffer> gpio_base;
  // Please do not use get_root_resource() in new code. See ZX-1467.
  zx::unowned_resource resource(get_root_resource());

  size_t aligned_size = ZX_ROUNDUP((S905D3_GPIO_BASE - kGpioBase) + S905D3_GPIO_LENGTH, PAGE_SIZE);

  status = ddk::MmioBuffer::Create(kGpioBase, aligned_size, *resource,
                                   ZX_CACHE_POLICY_UNCACHED_DEVICE, &gpio_base);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Create(gpio) error: %d", __func__, status);
  }

  PadDsReg2A::Get()
      .ReadFrom(&(*gpio_base))
      .set_gpiox_0_select(PadDsReg2A::kDriveStrengthMax)
      .set_gpiox_1_select(PadDsReg2A::kDriveStrengthMax)
      .set_gpiox_2_select(PadDsReg2A::kDriveStrengthMax)
      .set_gpiox_3_select(PadDsReg2A::kDriveStrengthMax)
      .set_gpiox_4_select(PadDsReg2A::kDriveStrengthMax)
      .set_gpiox_5_select(PadDsReg2A::kDriveStrengthMax)
      .WriteTo(&(*gpio_base));

  return status;
}

zx_status_t Nelson::SdioInit() {
  zx_status_t status;

  SdEmmcConfigurePortB();

  status =
      pbus_.CompositeDeviceAdd(&sd_emmc_dev, sdio_fragments, countof(sdio_fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd sd_emmc failed: %d", __func__, status);
    return status;
  }

  // Add a composite device for wifi driver.
  const zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_BROADCOM},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_BCM43458},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_BCM_WIFI},
  };

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = countof(props),
      .fragments = wifi_composite,
      .fragments_count = countof(wifi_composite),
      .coresident_device_index = 0,
      .metadata_list = nullptr,
      .metadata_count = 0,
  };

  status = DdkAddComposite("wifi", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAddComposite failed: %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace nelson
