// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/device-protocol/display-panel.h>
#include <lib/mmio/mmio.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/display.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <fbl/algorithm.h>
#include <hwreg/bitfields.h>
#include <soc/mt8167/mt8167-gpio.h>
#include <soc/mt8167/mt8167-hw.h>

#include "mt8167.h"

namespace board_mt8167 {

namespace {
constexpr pbus_mmio_t dsi_mmios[] = {
    // DSI0
    {
        .base = MT8167_DISP_DSI_BASE,
        .length = MT8167_DISP_DSI_SIZE,
    },
};

constexpr pbus_mmio_t display_mmios[] = {
    // Overlay
    {
        .base = MT8167_DISP_OVL_BASE,
        .length = MT8167_DISP_OVL_SIZE,
    },
    // Display RDMA
    {
        .base = MT8167_DISP_RDMA_BASE,
        .length = MT8167_DISP_RDMA_SIZE,
    },
    // MIPI_TX
    {
        .base = MT8167_MIPI_TX_BASE,
        .length = MT8167_MIPI_TX_SIZE,
    },
    // Display Mutex
    {
        .base = MT8167_DISP_MUTEX_BASE,
        .length = MT8167_DISP_MUTEX_SIZE,
    },
    // MT8167_MSYS_CFG_BASE
    {
        .base = MT8167_MSYS_CFG_BASE,
        .length = MT8167_MSYS_CFG_SIZE,
    },
    // Color
    {
        .base = MT8167_DISP_COLOR_BASE,
        .length = MT8167_DISP_COLOR_SIZE,
    },
    // AAL
    {
        .base = MT8167_DISP_AAL_BASE,
        .length = MT8167_DISP_AAL_SIZE,
    },
    // Dither
    {
        .base = MT8167_DITHER_BASE,
        .length = MT8167_DITHER_SIZE,
    },
    // Gamma
    {
        .base = MT8167_DISP_GAMMA_BASE,
        .length = MT8167_DISP_GAMMA_SIZE,
    },
    // CCORR
    {
        .base = MT8167_DISP_CCORR_BASE,
        .length = MT8167_DISP_CCORR_SIZE,
    },
    // SMI LARB0
    {
        .base = MT8167_DISP_SMI_LARB0_BASE,
        .length = MT8167_DISP_SMI_LARB0_SIZE,
    },
};

constexpr display_driver_t display_driver_info[] = {
    {
        .vid = PDEV_VID_MEDIATEK,
        .pid = PDEV_PID_MEDIATEK_8167S_REF,
        .did = PDEV_DID_MEDIATEK_DISPLAY,
    },
};

constexpr pbus_metadata_t display_metadata[] = {
    {
        .type = DEVICE_METADATA_DISPLAY_DEVICE,
        .data_buffer = &display_driver_info,
        .data_size = sizeof(display_driver_t),
    },
};

pbus_metadata_t display_panel_metadata[] = {
    {
        .type = DEVICE_METADATA_DISPLAY_CONFIG,
        .data_buffer = nullptr,
        .data_size = 0,
    },
};

constexpr pbus_bti_t display_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_DISPLAY,
    },
};
constexpr pbus_irq_t display_irqs[] = {
    {
        .irq = MT8167_IRQ_DISP_OVL0,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static pbus_dev_t display_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "display";
  dev.vid = PDEV_VID_MEDIATEK;
  dev.did = PDEV_DID_MEDIATEK_DISPLAY;
  dev.metadata_list = display_panel_metadata;
  dev.metadata_count = countof(display_panel_metadata);
  dev.mmio_list = display_mmios;
  dev.mmio_count = countof(display_mmios);
  dev.bti_list = display_btis;
  dev.bti_count = countof(display_btis);
  dev.irq_list = display_irqs;
  dev.irq_count = countof(display_irqs);
  return dev;
}();

static pbus_dev_t dsi_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "dw-dsi";
  dev.vid = PDEV_VID_MEDIATEK;
  dev.did = PDEV_DID_MEDIATEK_DSI;
  dev.metadata_list = display_metadata;
  dev.metadata_count = countof(display_metadata);
  dev.mmio_list = dsi_mmios;
  dev.mmio_count = countof(dsi_mmios);
  return dev;
}();

// Composite binding rules for display driver.
constexpr zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static const zx_bind_inst_t lcd_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, MT8167_GPIO_LCD_RST),
};
static const zx_bind_inst_t power_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_POWER),
    BI_MATCH_IF(EQ, BIND_POWER_DOMAIN, kVDLdoVGp2),
};
constexpr zx_bind_inst_t sysmem_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SYSMEM),
};
constexpr zx_bind_inst_t dsi_impl_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_DSI_IMPL),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_MEDIATEK),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_MEDIATEK_8167S_REF),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_MEDIATEK_DISPLAY),
};
static const device_fragment_part_t lcd_gpio_fragment[] = {
    {countof(root_match), root_match},
    {countof(lcd_gpio_match), lcd_gpio_match},
};
constexpr device_fragment_part_t sysmem_fragment[] = {
    {countof(root_match), root_match},
    {countof(sysmem_match), sysmem_match},
};
constexpr device_fragment_part_t dsi_impl_fragment[] = {
    {countof(root_match), root_match},
    {countof(dsi_impl_match), dsi_impl_match},
};
constexpr device_fragment_part_t power_fragment[] = {
    {countof(root_match), root_match},
    {countof(power_match), power_match},
};
constexpr device_fragment_t fragments[] = {
    {"gpio-lcd", countof(lcd_gpio_fragment), lcd_gpio_fragment},
    {"sysmem", countof(sysmem_fragment), sysmem_fragment},
    {"dsi", countof(dsi_impl_fragment), dsi_impl_fragment},
    {"power", countof(power_fragment), power_fragment},
};

}  // namespace

// TODO(payamm): Remove PMIC access once PMIC driver is ready
class WACS2_CMD : public hwreg::RegisterBase<WACS2_CMD, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<WACS2_CMD>(0x00A0); }

  DEF_BIT(31, WACS2_WRITE);
  DEF_FIELD(30, 16, WACS2_ADR);
  DEF_FIELD(15, 0, WACS2_WDATA);
};

class WACS2_RDATA : public hwreg::RegisterBase<WACS2_RDATA, uint32_t> {
 public:
  static constexpr uint32_t kStateIdle = 0;

  static auto Get() { return hwreg::RegisterAddr<WACS2_RDATA>(0x00A4); }

  DEF_FIELD(18, 16, status);
};

zx_status_t Mt8167::DisplayInit() {
  if (board_info_.pid != PDEV_PID_CLEO && board_info_.pid != PDEV_PID_MEDIATEK_8167S_REF) {
    zxlogf(ERROR, "Unsupported product");
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (board_info_.pid == PDEV_PID_MEDIATEK_8167S_REF) {
    // Enable Backlight on reference board only. Cleo has blacklight through I2C
    gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO55_DISP_PWM, MT8167_GPIO_GPIO_FN);
    gpio_impl_config_out(&gpio_impl_, MT8167_GPIO55_DISP_PWM, 1);
  }

  // TODO(payamm): Cannot use POWER_PROTOCOL since it does not support voltage selection yet
  // Enable LCD voltage rails
  uint32_t kVpg2VoSel = 0;
  if (board_info_.vid == PDEV_VID_MEDIATEK && board_info_.pid == PDEV_PID_MEDIATEK_8167S_REF) {
    kVpg2VoSel = 0x60;  // 3 << 5
  } else if (board_info_.vid == PDEV_VID_GOOGLE && board_info_.pid == PDEV_PID_CLEO) {
    kVpg2VoSel = 0xA0;  // 5 << 5
  } else {
    // make sure proper LCD voltage rail is set for any new PID
    ZX_DEBUG_ASSERT(false);
  }
  constexpr uint32_t kDigLdoCon29 = 0x0532;

  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx::unowned_resource root_resource(get_root_resource());
  std::optional<ddk::MmioBuffer> pmic_mmio;
  auto status =
      ddk::MmioBuffer::Create(MT8167_PMIC_WRAP_BASE, MT8167_PMIC_WRAP_SIZE, *root_resource,
                              ZX_CACHE_POLICY_UNCACHED_DEVICE, &pmic_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: PMIC MmioBuffer::Create failed %d", __FUNCTION__, status);
    return status;
  }

  // Wait for the PMIC to be IDLE.
  while (WACS2_RDATA::Get().ReadFrom(&(*pmic_mmio)).status() != WACS2_RDATA::kStateIdle) {
  }

  auto pmic = WACS2_CMD::Get().ReadFrom(&(*pmic_mmio));
  // From the documentation "Wrapper access: Address[15:1]" hence the >> 1.
  pmic.set_WACS2_WRITE(1).set_WACS2_ADR(kDigLdoCon29 >> 1).set_WACS2_WDATA(kVpg2VoSel);
  pmic.WriteTo(&(*pmic_mmio));

  status = pbus_.DeviceAdd(&dsi_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __FUNCTION__, status);
    return status;
  }

  display_panel_t display_panel_info[] = {
      {},
  };

  if (board_info_.pid == PDEV_PID_CLEO) {
    display_panel_info[0].width = 480;
    display_panel_info[0].height = 800;
    display_panel_info[0].panel_type = PANEL_ST7701S;
  } else {
    display_panel_info[0].width = 720;
    display_panel_info[0].height = 1280;
    display_panel_info[0].panel_type = PANEL_ILI9881C;
  }
  display_panel_metadata[0].data_size = sizeof(display_panel_info);
  display_panel_metadata[0].data_buffer = &display_panel_info;

  // Load display driver in same devhost as DSI driver.
  status = pbus_.CompositeDeviceAdd(&display_dev, fragments, std::size(fragments), 3);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd failed %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_mt8167
