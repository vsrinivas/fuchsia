// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/align.h>

#include <ddk/metadata/init-step.h>
#include <fbl/algorithm.h>
#include <hwreg/bitfields.h>
#include <soc/aml-common/aml-sdmmc.h>
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

static aml_sdmmc_config_t config = {
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
            {IOVAR_STR_TYPE, {"stbc_tx"}, 0},  // since tx_streams is 1
            {IOVAR_STR_TYPE, {"stbc_rx"}, 1},
            {IOVAR_CMD_TYPE, {.iovar_cmd = BRCMF_C_SET_PM}, 0},
            {IOVAR_CMD_TYPE, {.iovar_cmd = BRCMF_C_SET_FAKEFRAG}, 1},
            {IOVAR_LIST_END_TYPE, {{0}}, 0},
        },
    .cc_table =
        {
            {"WW", 2},   {"AU", 924}, {"CA", 902}, {"US", 844}, {"GB", 890}, {"BE", 890},
            {"BG", 890}, {"CZ", 890}, {"DK", 890}, {"DE", 890}, {"EE", 890}, {"IE", 890},
            {"GR", 890}, {"ES", 890}, {"FR", 890}, {"HR", 890}, {"IT", 890}, {"CY", 890},
            {"LV", 890}, {"LT", 890}, {"LU", 890}, {"HU", 890}, {"MT", 890}, {"NL", 890},
            {"AT", 890}, {"PL", 890}, {"PT", 890}, {"RO", 890}, {"SI", 890}, {"SK", 890},
            {"FI", 890}, {"SE", 890}, {"EL", 890}, {"IS", 890}, {"LI", 890}, {"TR", 890},
            {"CH", 890}, {"NO", 890}, {"JP", 3},   {"KR", 3},   {"TW", 3},   {"IN", 3},
            {"SG", 3},   {"MX", 3},   {"NZ", 3},   {"", 0},
        },
};

static const pbus_metadata_t sd_emmc_metadata[] = {
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = reinterpret_cast<const uint8_t*>(&config),
        .data_size = sizeof(config),
    },
    {
        .type = DEVICE_METADATA_WIFI_CONFIG,
        .data_buffer = reinterpret_cast<const uint8_t*>(&wifi_config),
        .data_size = sizeof(wifi_config),
    },
};

static const pbus_dev_t sd_emmc_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-sdio";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_AMLOGIC_SDMMC_A;
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
    {countof(sdio_fn1_match), sdio_fn1_match},
};
static const device_fragment_part_t sdio_fn2_fragment[] = {
    {countof(sdio_fn2_match), sdio_fn2_match},
};
static const device_fragment_part_t oob_gpio_fragment[] = {
    {countof(oob_gpio_match), oob_gpio_match},
};
static const device_fragment_t wifi_composite[] = {
    {"sdio-function-1", countof(sdio_fn1_fragment), sdio_fn1_fragment},
    {"sdio-function-2", countof(sdio_fn2_fragment), sdio_fn2_fragment},
    {"gpio-oob", countof(oob_gpio_fragment), oob_gpio_fragment},
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
    {countof(wifi_pwren_gpio_match), wifi_pwren_gpio_match},
};
constexpr device_fragment_part_t pwm_e_fragment[] = {
    {countof(pwm_e_match), pwm_e_match},
};
static const device_fragment_t sdio_fragments[] = {
    {"gpio-wifi-power-on", countof(wifi_pwren_gpio_fragment), wifi_pwren_gpio_fragment},
    {"pwm", countof(pwm_e_fragment), pwm_e_fragment},
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
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
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

  status = pbus_.CompositeDeviceAdd(&sd_emmc_dev, reinterpret_cast<uint64_t>(sdio_fragments),
                                    countof(sdio_fragments), UINT32_MAX);
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
      .primary_fragment = "sdio-function-1",  // ???
      .spawn_colocated = true,
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
