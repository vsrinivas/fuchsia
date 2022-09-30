// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <iterator>
#include <memory>

#include <fbl/algorithm.h>

#include "src/devices/bus/drivers/platform/test/test-board-bind.h"
#include "src/devices/bus/drivers/platform/test/test-metadata.h"
#include "src/devices/bus/drivers/platform/test/test-resources.h"
#include "src/devices/bus/drivers/platform/test/test.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace board_test {

void TestBoard::DdkRelease() { delete this; }

int TestBoard::Thread() {
  zx_status_t status;
  status = GpioInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: GpioInit failed: %d", __func__, status);
  }

  status = I2cInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: I2cInit failed: %d", __func__, status);
  }

  status = SpiInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: SpiInit failed: %d", __func__, status);
  }

  status = ClockInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ClockInit failed: %d", __func__, status);
  }

  status = PowerInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: PowerInit failed: %d", __func__, status);
  }

  status = TestInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: TestInit failed: %d", __func__, status);
  }

  status = PwmInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: PwmInit failed: %d", __func__, status);
  }

  status = VregInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: VregInit failed: %d", __func__, status);
  }

  status = PciInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: PciInit failed: %d", __func__, status);
  }

  status = PowerSensorInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: PowerSensorInit failed: %d", __func__, status);
  }

  return 0;
}

zx_status_t TestBoard::Start() {
  int rc = thrd_create_with_name(
      &thread_, [](void* arg) -> int { return reinterpret_cast<TestBoard*>(arg)->Thread(); }, this,
      "test-board-start-thread");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t TestBoard::Create(zx_device_t* parent) {
  auto endpoints = fdf::CreateEndpoints<fuchsia_hardware_platform_bus::PlatformBus>();
  if (endpoints.is_error()) {
    return endpoints.error_value();
  }

  zx_status_t status = device_connect_runtime_protocol(
      parent, fuchsia_hardware_platform_bus::Service::PlatformBus::ServiceName,
      fuchsia_hardware_platform_bus::Service::PlatformBus::Name,
      endpoints->server.TakeHandle().release());
  if (status != ZX_OK) {
    return status;
  }

  auto board = std::make_unique<TestBoard>(parent, std::move(endpoints->client));
  auto& pbus = board->pbus_;

  status = board->DdkAdd("test-board", DEVICE_ADD_NON_BINDABLE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "TestBoard::Create: DdkAdd failed: %d", status);
    return status;
  }

  status = board->Start();
  if (status == ZX_OK) {
    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = board.release();
  }

  // Add a composite device
  const zx_bind_inst_t power_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_POWER),
      BI_MATCH_IF(EQ, BIND_POWER_DOMAIN, 3),
  };
  device_fragment_part_t power_fragment[] = {
      {std::size(power_match), power_match},
  };
  const zx_bind_inst_t gpio_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
      BI_MATCH_IF(EQ, BIND_GPIO_PIN, 3),
  };
  const zx_bind_inst_t clock_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
      BI_MATCH_IF(EQ, BIND_CLOCK_ID, 1),
  };
  const zx_bind_inst_t i2c_match[] = {
      BI_ABORT_IF(NE, BIND_FIDL_PROTOCOL, ZX_FIDL_PROTOCOL_I2C),
      BI_ABORT_IF(NE, BIND_I2C_BUS_ID, 1),
      BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 5),
  };
  const zx_bind_inst_t child4_match[] = {
      BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
      BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_PBUS_TEST),
      BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TEST_CHILD_4),
  };
  const zx_bind_inst_t spi_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_SPI),
      BI_ABORT_IF(NE, BIND_SPI_BUS_ID, 0),
      BI_MATCH_IF(EQ, BIND_SPI_CHIP_SELECT, 0),
  };
  const zx_bind_inst_t pwm_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PWM),
      BI_MATCH_IF(EQ, BIND_PWM_ID, 0),
  };
  const zx_bind_inst_t vreg_match[] = {
      BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_VREG),
  };
  const zx_bind_inst_t pci_match[] = {
      BI_MATCH_IF(EQ, BIND_FIDL_PROTOCOL, ZX_FIDL_PROTOCOL_PCI),
  };
  const zx_bind_inst_t power_sensor_match[] = {
      BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_POWER_SENSOR),
  };
  device_fragment_part_t gpio_fragment[] = {
      {std::size(gpio_match), gpio_match},
  };
  device_fragment_part_t clock_fragment[] = {
      {std::size(clock_match), clock_match},
  };
  device_fragment_part_t i2c_fragment[] = {
      {std::size(i2c_match), i2c_match},
  };
  device_fragment_part_t child4_fragment[] = {
      {std::size(child4_match), child4_match},
  };
  device_fragment_part_t spi_fragment[] = {
      {std::size(spi_match), spi_match},
  };
  device_fragment_part_t pwm_fragment[] = {
      {std::size(pwm_match), pwm_match},
  };
  device_fragment_part_t vreg_fragment[] = {
      {std::size(vreg_match), vreg_match},
  };
  device_fragment_part_t pci_fragment[] = {
      {std::size(pci_match), pci_match},
  };
  device_fragment_part_t power_sensor_fragment[] = {
      {std::size(power_sensor_match), power_sensor_match},
  };

  device_fragment_t composite[] = {
      {"gpio", std::size(gpio_fragment), gpio_fragment},
      {"clock", std::size(clock_fragment), clock_fragment},
      {"i2c", std::size(i2c_fragment), i2c_fragment},
      {"power", std::size(power_fragment), power_fragment},
      {"child4", std::size(child4_fragment), child4_fragment},
  };

  struct composite_test_metadata metadata_1 = {
      .composite_device_id = PDEV_DID_TEST_COMPOSITE_1,
      .metadata_value = 12345,
  };

  struct composite_test_metadata metadata_2 = {
      .composite_device_id = PDEV_DID_TEST_COMPOSITE_2,
      .metadata_value = 12345,
  };

  const std::vector<fuchsia_hardware_platform_bus::Metadata> test_metadata_1{
      [&]() {
        fuchsia_hardware_platform_bus::Metadata ret;
        ret.type() = DEVICE_METADATA_PRIVATE;
        ret.data() = std::vector(reinterpret_cast<uint8_t*>(&metadata_1),
                                 reinterpret_cast<uint8_t*>(&metadata_1) + sizeof(metadata_1));
        return ret;
      }(),
  };

  const std::vector<fuchsia_hardware_platform_bus::Metadata> test_metadata_2{
      [&]() {
        fuchsia_hardware_platform_bus::Metadata ret;
        ret.type() = DEVICE_METADATA_PRIVATE;
        ret.data() = std::vector(reinterpret_cast<uint8_t*>(&metadata_2),
                                 reinterpret_cast<uint8_t*>(&metadata_2) + sizeof(metadata_2));
        return ret;
      }(),
  };

  fuchsia_hardware_platform_bus::Node pdev = {};
  pdev.name() = "composite-dev";
  pdev.vid() = PDEV_VID_TEST;
  pdev.pid() = PDEV_PID_PBUS_TEST;
  pdev.did() = PDEV_DID_TEST_COMPOSITE_1;
  pdev.metadata() = std::move(test_metadata_1);

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('TEST');
  auto result = pbus.buffer(arena)->AddCompositeImplicitPbusFragment(
      fidl::ToWire(fidl_arena, std::move(pdev)),
      platform_bus_composite::MakeFidlFragment(fidl_arena, composite, std::size(composite)), {});
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  device_fragment_t composite2[] = {
      {"clock", std::size(clock_fragment), clock_fragment},
      {"power", std::size(power_fragment), power_fragment},
      {"child4", std::size(child4_fragment), child4_fragment},
      {"spi", std::size(spi_fragment), spi_fragment},
      {"pwm", std::size(pwm_fragment), pwm_fragment},
      {"vreg", std::size(vreg_fragment), vreg_fragment},
      {"pci", std::size(pci_fragment), pci_fragment},
      {"power-sensor", std::size(power_sensor_fragment), power_sensor_fragment},
  };

  fuchsia_hardware_platform_bus::Node pdev2 = {};
  pdev2.name() = "composite-dev-2";
  pdev2.vid() = PDEV_VID_TEST;
  pdev2.pid() = PDEV_PID_PBUS_TEST;
  pdev2.did() = PDEV_DID_TEST_COMPOSITE_2;
  pdev2.metadata() = std::move(test_metadata_2);

  result = pbus.buffer(arena)->AddCompositeImplicitPbusFragment(
      fidl::ToWire(fidl_arena, std::move(pdev2)),
      platform_bus_composite::MakeFidlFragment(fidl_arena, composite2, std::size(composite2)), {});
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return status;
}

zx_status_t test_bind(void* ctx, zx_device_t* parent) { return TestBoard::Create(parent); }

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = test_bind;
  return ops;
}();

}  // namespace board_test

ZIRCON_DRIVER(test_board, board_test::driver_ops, "zircon", "0.1");
