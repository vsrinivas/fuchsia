// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/clockimpl/cpp/banjo.h>
#include <fuchsia/hardware/gpioimpl/cpp/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-common/aml-registers.h>
#include <soc/aml-meson/g12b-clk.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock-gpios.h"
#include "sherlock.h"
#include "src/devices/board/drivers/sherlock/camera-controller-bind.h"
#include "src/devices/board/drivers/sherlock/camera-gdc-bind.h"
#include "src/devices/board/drivers/sherlock/camera-ge2d-bind.h"
#include "src/devices/board/drivers/sherlock/camera-isp-bind.h"
#include "src/devices/board/drivers/sherlock/imx227-sensor-bind.h"

namespace sherlock {

namespace {

constexpr uint32_t kClk24MAltFunc = 7;
constexpr uint32_t kClkGpioDriveStrengthUa = 4000;

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
  dev.pid = PDEV_PID_ARM_ISP;
  dev.did = PDEV_DID_ARM_MALI_IV009;
  dev.mmio_list = isp_mmios;
  dev.mmio_count = countof(isp_mmios);
  dev.bti_list = isp_btis;
  dev.bti_count = countof(isp_btis);
  dev.irq_list = isp_irqs;
  dev.irq_count = countof(isp_irqs);
  return dev;
}();

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
const pbus_dev_t sensor_dev_sherlock = []() {
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
  gpio_impl_.SetDriveStrength(T931_GPIOAO(10), kClkGpioDriveStrengthUa, nullptr);

  zx_status_t status = pbus_.DeviceAdd(&mipi_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Mipi_Device DeviceAdd failed %d", __func__, status);
    return status;
  }

  status =
      pbus_.AddComposite(&sensor_dev_sherlock, reinterpret_cast<uint64_t>(imx227_sensor_fragments),
                         countof(imx227_sensor_fragments), "mipicsi");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Camera Sensor DeviceAdd failed %d", __func__, status);
    return status;
  }

  status = pbus_.AddComposite(&gdc_dev, reinterpret_cast<uint64_t>(gdc_fragments),
                              countof(gdc_fragments), "camera-sensor");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: GDC DeviceAdd failed %d", __func__, status);
    return status;
  }

  status = pbus_.AddComposite(&ge2d_dev, reinterpret_cast<uint64_t>(ge2d_fragments),
                              countof(ge2d_fragments), "camera-sensor");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: GE2D DeviceAdd failed %d", __func__, status);
    return status;
  }

  // Add a composite device for ARM ISP
  status = pbus_.AddComposite(&isp_dev, reinterpret_cast<uint64_t>(isp_fragments),
                              countof(isp_fragments), "camera-sensor");

  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ISP DeviceAdd failed %d", __func__, status);
    return status;
  }

  constexpr zx_device_prop_t camera_controller_props[] = {
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_CAMERA_CONTROLLER},
  };

  const composite_device_desc_t camera_comp_desc = {
      .props = camera_controller_props,
      .props_count = countof(camera_controller_props),
      .fragments = camera_controller_fragments,
      .fragments_count = countof(camera_controller_fragments),
      .primary_fragment = "isp",
      .spawn_colocated = true,
      .metadata_list = nullptr,
      .metadata_count = 0,
  };

  status = DdkAddComposite("camera-controller", &camera_comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Camera Controller DeviceAdd failed %d", __func__, status);
    return status;
  }

  return status;
}

}  // namespace sherlock
