// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddktl/protocol/clockimpl.h>
#include <ddktl/protocol/gpioimpl.h>
#include <soc/aml-meson/g12b-clk.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock-gpios.h"
#include "sherlock.h"

namespace sherlock {

namespace {

constexpr uint32_t kClk24MAltFunc = 7;
constexpr uint32_t kClkGpioDriveStrength = 3;

// TODO(CAM-138): This is a temporary hack. Remove when new driver validated.
constexpr bool kUseArmDriver = false;

constexpr pbus_mmio_t ge2d_mmios[] = {
    // GE2D Base
    {
        .base = T931_GE2D_BASE,
        .length = T931_GE2D_LENGTH,
    },
};

constexpr pbus_bti_t ge2d_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_GE2D,
    },
};

// IRQ for GE2D
constexpr pbus_irq_t ge2d_irqs[] = {
    {
        .irq = T931_MALI_GE2D_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static pbus_dev_t ge2d_dev = []() {
  // GE2D
  pbus_dev_t dev = {};
  dev.name = "ge2d";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_T931;
  dev.did = PDEV_DID_AMLOGIC_GE2D;
  dev.mmio_list = ge2d_mmios;
  dev.mmio_count = countof(ge2d_mmios);
  dev.bti_list = ge2d_btis;
  dev.bti_count = countof(ge2d_btis);
  dev.irq_list = ge2d_irqs;
  dev.irq_count = countof(ge2d_irqs);
  return dev;
}();

constexpr pbus_mmio_t gdc_mmios[] = {
    // HIU for clocks.
    {
        .base = T931_HIU_BASE,
        .length = T931_HIU_LENGTH,
    },
    // GDC Base
    {
        .base = T931_GDC_BASE,
        .length = T931_GDC_LENGTH,
    },
};

constexpr pbus_bti_t gdc_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_GDC,
    },
};

// IRQ for ISP
constexpr pbus_irq_t gdc_irqs[] = {
    {
        .irq = T931_MALI_GDC_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static pbus_dev_t gdc_dev = []() {
  // GDC
  pbus_dev_t dev = {};
  dev.name = "gdc";
  dev.vid = PDEV_VID_ARM;
  dev.pid = PDEV_PID_GDC;
  dev.did = PDEV_DID_ARM_MALI_IV010;
  dev.mmio_list = gdc_mmios;
  dev.mmio_count = countof(gdc_mmios);
  dev.bti_list = gdc_btis;
  dev.bti_count = countof(gdc_btis);
  dev.irq_list = gdc_irqs;
  dev.irq_count = countof(gdc_irqs);
  return dev;
}();

constexpr pbus_bti_t isp_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_ISP,
    },
};

constexpr pbus_mmio_t isp_mmios[] = {
    // HIU for clocks.
    {
        .base = T931_HIU_BASE,
        .length = T931_HIU_LENGTH,
    },
    // Power domain
    {
        .base = T931_POWER_DOMAIN_BASE,
        .length = T931_POWER_DOMAIN_LENGTH,
    },
    // Memory PD
    {
        .base = T931_MEMORY_PD_BASE,
        .length = T931_MEMORY_PD_LENGTH,
    },
    // Reset
    {
        .base = T931_RESET_BASE,
        .length = T931_RESET_LENGTH,
    },
    // ISP Base
    {
        .base = T931_ISP_BASE,
        .length = T931_ISP_LENGTH,
    },
};

// IRQ for ISP
static const pbus_irq_t isp_irqs[] = {
    {
        .irq = T931_MALI_ISP_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static pbus_dev_t isp_dev = []() {
  // ISP
  pbus_dev_t dev = {};
  dev.name = "isp";
  dev.vid = PDEV_VID_ARM;
  dev.pid = PDEV_PID_ISP;
  dev.did = PDEV_DID_ARM_MALI_IV009;
  dev.mmio_list = isp_mmios;
  dev.mmio_count = countof(isp_mmios);
  dev.bti_list = isp_btis;
  dev.bti_count = countof(isp_btis);
  dev.irq_list = isp_irqs;
  dev.irq_count = countof(isp_irqs);
  return dev;
}();

// TODO(CAM-138): This is a temporary hack. Remove when new driver validated.
static pbus_dev_t isp_dev_v2 = []() {
  // ISP using ARM driver.
  pbus_dev_t dev = {};
  dev.name = "isp";
  dev.vid = PDEV_VID_ARM;
  dev.pid = PDEV_PID_ISP_BARE_METAL;
  dev.did = PDEV_DID_ARM_MALI_IV009;
  dev.mmio_list = isp_mmios;
  dev.mmio_count = countof(isp_mmios);
  dev.bti_list = isp_btis;
  dev.bti_count = countof(isp_btis);
  dev.irq_list = isp_irqs;
  dev.irq_count = countof(isp_irqs);
  return dev;
}();

// Composite binding rules for ARM ISP
static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static const zx_bind_inst_t camera_sensor_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_CAMERA_SENSOR),
};

static const zx_bind_inst_t amlogiccanvas_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_AMLOGIC_CANVAS),
};
static const device_component_part_t camera_sensor_component[] = {
    {countof(root_match), root_match},
    {countof(camera_sensor_match), camera_sensor_match},
};
static const device_component_t isp_components[] = {
    {countof(camera_sensor_component), camera_sensor_component},
};

// Compisite binding rules for GDC
static const device_component_t gdc_components[] = {
    {countof(camera_sensor_component), camera_sensor_component},
};

static const device_component_part_t amlogiccanvas_component[] = {
    {countof(root_match), root_match},
    {countof(amlogiccanvas_match), amlogiccanvas_match},
};

// Composite binding rules for GE2D
static const device_component_t ge2d_components[] = {
    {countof(camera_sensor_component), camera_sensor_component},
    {countof(amlogiccanvas_component), amlogiccanvas_component},
};

// Composite binding rules for IMX227 Sensor.
static const zx_bind_inst_t i2c_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, SHERLOCK_I2C_3),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x36),
};
static const zx_bind_inst_t gpio_reset_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_CAM_RESET),
};
static const zx_bind_inst_t gpio_vana_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_VANA_ENABLE),
};
static const zx_bind_inst_t gpio_vdig_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_VDIG_ENABLE),
};
static const zx_bind_inst_t clk_sensor_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, g12b_clk::G12B_CLK_CAM_INCK_24M),
};
static const zx_bind_inst_t mipicsi_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MIPI_CSI),
};
static const device_component_part_t i2c_component[] = {
    {countof(root_match), root_match},
    {countof(i2c_match), i2c_match},
};
static const device_component_part_t gpio_reset_component[] = {
    {countof(root_match), root_match},
    {countof(gpio_reset_match), gpio_reset_match},
};
static const device_component_part_t gpio_vana_component[] = {
    {countof(root_match), root_match},
    {countof(gpio_vana_match), gpio_vana_match},
};
static const device_component_part_t gpio_vdig_component[] = {
    {countof(root_match), root_match},
    {countof(gpio_vdig_match), gpio_vdig_match},
};
static const device_component_part_t clk_sensor_component[] = {
    {countof(root_match), root_match},
    {countof(clk_sensor_match), clk_sensor_match},
};
static const device_component_part_t mipicsi_component[] = {
    {countof(root_match), root_match},
    {countof(mipicsi_match), mipicsi_match},
};
static const device_component_t imx227_sensor_components[] = {
    {countof(mipicsi_component), mipicsi_component},
    {countof(i2c_component), i2c_component},
    {countof(gpio_vana_component), gpio_vana_component},
    {countof(gpio_vdig_component), gpio_vdig_component},
    {countof(gpio_reset_component), gpio_reset_component},
    {countof(clk_sensor_component), clk_sensor_component},
};

// Composite device binding rules for Camera Controller
static const zx_bind_inst_t isp_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_ISP),
};
static const zx_bind_inst_t gdc_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_GDC),
};
static const zx_bind_inst_t ge2d_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_GE2D),
};
static const zx_bind_inst_t sysmem_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SYSMEM),
};
static const zx_bind_inst_t buttons_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_BUTTONS),
};
static const device_component_part_t isp_component[] = {
    {countof(root_match), root_match},
    {countof(isp_match), isp_match},
};
static const device_component_part_t gdc_component[] = {
    {countof(root_match), root_match},
    {countof(gdc_match), gdc_match},
};
static const device_component_part_t ge2d_component[] = {
    {countof(root_match), root_match},
    {countof(ge2d_match), ge2d_match},
};
static const device_component_part_t sysmem_component[] = {
    {countof(root_match), root_match},
    {countof(sysmem_match), sysmem_match},
};
static const device_component_part_t buttons_component[] = {
    {countof(root_match), root_match},
    {countof(buttons_match), buttons_match},
};
static const device_component_t camera_controller_components[] = {
    {countof(isp_component), isp_component},         {countof(gdc_component), gdc_component},
    {countof(gdc_component), ge2d_component},        {countof(sysmem_component), sysmem_component},
    {countof(buttons_component), buttons_component},
};

constexpr pbus_mmio_t mipi_mmios[] = {
    // CSI PHY0
    {
        .base = T931_CSI_PHY0_BASE,
        .length = T931_CSI_PHY0_LENGTH,
    },
    // Analog PHY
    {
        .base = T931_APHY_BASE,
        .length = T931_APHY_LENGTH,
    },
    // CSI HOST0
    {
        .base = T931_CSI_HOST0_BASE,
        .length = T931_CSI_HOST0_LENGTH,
    },
    // MIPI Adapter
    {
        .base = T931_MIPI_ADAPTER_BASE,
        .length = T931_MIPI_ADAPTER_LENGTH,
    },
    // HIU for clocks.
    {
        .base = T931_HIU_BASE,
        .length = T931_HIU_LENGTH,
    },
};

constexpr pbus_bti_t mipi_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_MIPI,
    },
};

constexpr pbus_irq_t mipi_irqs[] = {
    {
        .irq = T931_MIPI_ADAPTER_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

// Binding rules for MIPI Driver
static const pbus_dev_t mipi_dev = []() {
  // MIPI CSI PHY ADAPTER
  pbus_dev_t dev = {};
  dev.name = "mipi-csi2";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_T931;
  dev.did = PDEV_DID_AMLOGIC_MIPI_CSI;
  dev.mmio_list = mipi_mmios;
  dev.mmio_count = countof(mipi_mmios);
  dev.bti_list = mipi_btis;
  dev.bti_count = countof(mipi_btis);
  dev.irq_list = mipi_irqs;
  dev.irq_count = countof(mipi_irqs);
  return dev;
}();

// Binding rules for Sensor Driver
const pbus_dev_t sensor_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "imx227-sensor";
  dev.vid = PDEV_VID_SONY;
  dev.pid = PDEV_PID_SONY_IMX227;
  dev.did = PDEV_DID_CAMERA_SENSOR;
  return dev;
}();

}  // namespace

// Refer to camera design document for driver
// design and layout details.
zx_status_t Sherlock::CameraInit() {
  // Set GPIO alternate functions.
  gpio_impl_.SetAltFunction(T931_GPIOAO(10), kClk24MAltFunc);
  gpio_impl_.SetDriveStrength(T931_GPIOAO(10), kClkGpioDriveStrength);

  zx_status_t status = pbus_.DeviceAdd(&mipi_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Mipi_Device DeviceAdd failed %d\n", __func__, status);
    return status;
  }

  status = pbus_.CompositeDeviceAdd(&sensor_dev, imx227_sensor_components,
                                    countof(imx227_sensor_components), 1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: IMX227 DeviceAdd failed %d\n", __func__, status);
    return status;
  }

  status = pbus_.CompositeDeviceAdd(&gdc_dev, gdc_components, countof(gdc_components), 1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: GDC DeviceAdd failed %d\n", __func__, status);
    return status;
  }

  status = pbus_.CompositeDeviceAdd(&ge2d_dev, ge2d_components, countof(ge2d_components), 1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: GE2D DeviceAdd failed %d\n", __func__, status);
    return status;
  }

  // Add a composite device for ARM ISP
  // TODO(CAM-138): This is a temporary hack. Remove when new driver validated.
  if (kUseArmDriver) {
    status = pbus_.CompositeDeviceAdd(&isp_dev_v2, isp_components, countof(isp_components), 1);
  } else {
    status = pbus_.CompositeDeviceAdd(&isp_dev, isp_components, countof(isp_components), 1);
  }
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ISP DeviceAdd failed %d\n", __func__, status);
    return status;
  }

  constexpr zx_device_prop_t camera_controller_props[] = {
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_CAMERA_CONTROLLER},
  };

  const composite_device_desc_t camera_comp_desc = {
      .props = camera_controller_props,
      .props_count = countof(camera_controller_props),
      .components = camera_controller_components,
      .components_count = countof(camera_controller_components),
      .coresident_device_index = 0,
      .metadata_list = nullptr,
      .metadata_count = 0,
  };

  status = DdkAddComposite("camera-controller", &camera_comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Camera Controller DeviceAdd failed %d\n", __func__, status);
    return status;
  }

  return status;
}

}  // namespace sherlock
