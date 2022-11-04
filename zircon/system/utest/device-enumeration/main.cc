// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.driver.development/cpp/wire.h>
#include <fidl/fuchsia.sysinfo/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/stdcompat/span.h>
#include <lib/sys/component/cpp/service_client.h>
#include <stdlib.h>
#include <unistd.h>
#include <zircon/status.h>

#include <iostream>
#include <iterator>
#include <unordered_set>

#include <fbl/algorithm.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <zxtest/base/log-sink.h>
#include <zxtest/zxtest.h>

#include "src/lib/fsl/io/device_watcher.h"
#include "zircon/system/utest/device-enumeration/aemu.h"

namespace {

bool IsDfv2Enabled() {
  zx::result driver_development =
      component::Connect<fuchsia_driver_development::DriverDevelopment>();
  EXPECT_OK(driver_development.status_value());

  const fidl::WireResult result = fidl::WireCall(driver_development.value())->IsDfv2();
  EXPECT_OK(result.status());
  return result.value().response;
}

// Asyncronously wait for a path to appear, and call `callback` when the path exists.
// The `watchers` array is needed because each directory in the path needs to allocate a
// DeviceWatcher, and they need to be stored somewhere that can be freed later.
void RecursiveWaitFor(const std::string& full_path, size_t slash_index,
                      fit::function<void()> callback,
                      std::vector<std::unique_ptr<fsl::DeviceWatcher>>& watchers,
                      async_dispatcher_t* dispatcher) {
  if (slash_index == full_path.size()) {
    fprintf(stderr, "Found %s \n", full_path.c_str());
    callback();
    return;
  }

  const std::string dir_path = full_path.substr(0, slash_index);
  size_t next_slash = full_path.find('/', slash_index + 1);
  if (next_slash == std::string::npos) {
    next_slash = full_path.size();
  }
  const std::string file_name = full_path.substr(slash_index + 1, next_slash - (slash_index + 1));

  watchers.push_back(fsl::DeviceWatcher::Create(
      dir_path,
      [file_name, full_path, next_slash, callback = std::move(callback), &watchers, dispatcher](
          int dir_fd, const std::string& name) mutable {
        if (name == file_name) {
          RecursiveWaitFor(full_path, next_slash, std::move(callback), watchers, dispatcher);
        }
      },
      dispatcher));
}

void WaitForOne(cpp20::span<const char*> device_paths) {
  async::Loop loop = async::Loop(&kAsyncLoopConfigNeverAttachToThread);

  async::TaskClosure task([device_paths]() {
    // stdout doesn't show up in test logs.
    fprintf(stderr, "still waiting for device paths:\n");
    for (const char* path : device_paths) {
      fprintf(stderr, " %s\n", path);
    }
  });
  ASSERT_OK(task.PostDelayed(loop.dispatcher(), zx::min(1)));

  std::vector<std::unique_ptr<fsl::DeviceWatcher>> watchers;
  for (const char* path : device_paths) {
    RecursiveWaitFor(
        std::string("/dev/") + path, 4, [&loop]() { loop.Shutdown(); }, watchers,
        loop.dispatcher());
  }

  loop.Run();
}

fbl::String GetTestFilter() {
  zx::result sys_info = component::Connect<fuchsia_sysinfo::SysInfo>();
  if (sys_info.is_error()) {
    return "Unknown";
  }

  const fidl::WireResult result = fidl::WireCall(sys_info.value())->GetBoardName();
  if (!result.ok()) {
    return "Unknown";
  }
  const fidl::WireResponse response = result.value();
  if (response.status != ZX_OK) {
    return "Unknown";
  }
  const std::string_view board_name = response.name.get();

  std::cout << "Found board " << board_name << std::endl;

  if (board_name == "qemu") {
    return "*QemuArm64*";
  } else if (board_name == "vim3") {
    return "*Vim3*";
  } else if (board_name == "astro") {
    return "*Astro*";
  } else if (board_name == "sherlock") {
    return "*Sherlock*";
  } else if (board_name == "msm8x53-som") {
    return "*Msm8x53Som*";
  } else if (board_name == "as370" || board_name == "visalia") {
    return "*Visalia*";
  } else if (board_name == "pinecrest") {
    return "*Pinecrest*";
  } else if (board_name == "Nocturne") {
    return "*Nocturne*";
  } else if (board_name == "nelson") {
    return "*Nelson*";
  } else if (board_name == "luis") {
    return "*Luis*";
  } else if (board_name == "Eve") {
    return "*Eve*";
  } else if (board_name == "NUC7i5DNB") {
    return "*Nuc*";
  } else if (board_name == "Atlas") {
    return "*Atlas*";
  } else if (board_name == "Standard PC (Q35 + ICH9, 2009)") {
    // QEMU and AEMU with emulated Q35 boards have this board name.
    return "*QemuX64Q35*";
  } else if (board_name == "av400") {
    return "*Av400*";
  } else if (board_name == "Google Compute Engine") {
#ifdef __aarch64__
    return "*GceArm64*";
#endif
  } else if (board_name == "arm64" || board_name == "x64") {
    return "*GenericShouldFail*";
  } else if (board_name == "clover") {
    return "*Clover*";
  }

  return "Unknown";
}

class DeviceEnumerationTest : public zxtest::Test {
  void SetUp() override { ASSERT_NO_FATAL_FAILURE(PrintAllDevices()); }

 protected:
  static void TestRunner(const char** device_paths, size_t paths_num) {
    async::Loop loop = async::Loop(&kAsyncLoopConfigNeverAttachToThread);

    std::unordered_set<const char*> device_paths_set;
    for (size_t i = 0; i < paths_num; ++i) {
      device_paths_set.emplace(device_paths[i]);
    }
    async::TaskClosure task;
    std::vector<std::unique_ptr<fsl::DeviceWatcher>> watchers;
    {
      // Intentionally shadow.
      std::unordered_set<const char*>& device_paths = device_paths_set;
      task.set_handler([&device_paths]() {
        // stdout doesn't show up in test logs.
        fprintf(stderr, "still waiting for device paths:\n");
        for (const char* path : device_paths) {
          fprintf(stderr, " %s\n", path);
        }
      });
      ASSERT_OK(task.PostDelayed(loop.dispatcher(), zx::min(1)));

      for (const char* path : device_paths) {
        RecursiveWaitFor(
            std::string("/dev/") + path, 4,
            [&loop, &device_paths, path]() {
              ASSERT_EQ(device_paths.erase(path), 1);
              if (device_paths.empty()) {
                loop.Shutdown();
              }
            },
            watchers, loop.dispatcher());
      }
    }
    loop.Run();
  }

 private:
  static void PrintAllDevices() {
    // This uses the development API for its convenience over directory traversal. It would be more
    // useful to log paths in devfs for the purposes of this test, but less convenient.
    zx::result driver_development =
        component::Connect<fuchsia_driver_development::DriverDevelopment>();
    ASSERT_OK(driver_development.status_value());

    const fidl::WireResult result = fidl::WireCall(driver_development.value())->IsDfv2();
    ASSERT_OK(result.status());
    const bool is_dfv2 = result.value().response;

    {
      zx::result endpoints =
          fidl::CreateEndpoints<fuchsia_driver_development::DeviceInfoIterator>();
      ASSERT_OK(endpoints.status_value());
      auto& [client, server] = endpoints.value();

      const fidl::WireResult result =
          fidl::WireCall(driver_development.value())->GetDeviceInfo({}, std::move(server));
      ASSERT_OK(result.status());

      // NB: this uses iostream (rather than printf) because FIDL strings aren't null-terminated.
      std::cout << "BEGIN printing all devices (paths in DFv1, monikers in DFv2):" << std::endl;
      while (true) {
        const fidl::WireResult result = fidl::WireCall(client)->GetNext();
        ASSERT_OK(result.status());
        const fidl::WireResponse response = result.value();
        if (response.drivers.empty()) {
          break;
        }
        for (const fuchsia_driver_development::wire::DeviceInfo& info : response.drivers) {
          if (is_dfv2) {
            ASSERT_TRUE(info.has_moniker());
            std::cout << info.moniker().get() << std::endl;
          } else {
            ASSERT_TRUE(info.has_topological_path());
            std::cout << info.topological_path().get() << std::endl;
          }
        }
      }
      std::cout << "END printing all devices (paths in DFv1, monikers in DFv2)." << std::endl;
    }
  }
};

TEST_F(DeviceEnumerationTest, CloverTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/pt/clover",
      "sys/platform/05:08:1/aml-gpio",
      "sys/platform/05:08:32/clocks",
      "sys/platform/00:00:1b/sysmem",
      "sys/platform/00:00:e/tee/optee",
      "sys/platform/05:08:a/thermal",
      "class/thermal/000",
      "sys/platform/05:08:24/ram",
      "sys/platform/05:00:2/aml-i2c",
      "sys/platform/05:00:19/spi-0/aml-spi-0/spi/spi-0-0",
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, Av400Test) {
  static const char* kDevicePaths[] = {
      "sys/platform/pt/av400",
      "sys/platform/05:07:1/aml-gpio",
      "sys/platform/05:07:1d",  // pwm
      "sys/platform/05:07:2c/clocks",
      "sys/platform/05:00:2/aml-i2c",
      "sys/platform/00:00:29",  // registers device
      "sys/platform/05:07:8/aml_emmc/aml-sd-emmc/sdmmc/sdmmc-mmc",
      "sys/platform/05:00:6/aml_sdio/aml-sd-emmc/sdmmc/sdmmc-sdio",
      "sys/platform/05:00:19/spi-1/aml-spi-1/spi/spi-1-0",
      "sys/platform/05:07:1d/aml-pwm-device/pwm-6/pwm-init",
      "sys/platform/05:07:9/ethernet_mac/aml-ethernet/dwmac/dwmac/eth_phy/phy_null_device",
      "sys/platform/05:07:9/ethernet_mac/aml-ethernet/dwmac/dwmac/Designware-MAC/ethernet",
      "sys/platform/05:07:9/ethernet_mac/aml-ethernet",
      "sys/platform/05:07:2e/aml-rtc",
      "sys/platform/05:07:12:1/av400-i2s-audio-out",
      "sys/platform/05:07:12:2/av400-i2s-audio-in",
      "sys/platform/05:07:13/av400-audio-pdm-in",
      "sys/platform/05:07:b/aml-mailbox",
      "sys/platform/05:07:31/dsp/aml-dsp",

      // CPU Device
      "sys/platform/05:07:1e",
      "class/cpu-ctrl/000",
      "sys/platform/05:07:26/aml-power-impl-composite/power-impl/composite-pd-armcore/power-0/aml-cpu/a5-arm-a55",

      // USB
      "sys/platform/05:00:2f/aml-usb-crg-phy-v2",
      // Force to usb peripheral
      "sys/platform/05:00:2f/aml-usb-crg-phy-v2/aml-usb-crg-phy-v2/udc/udc/udc/usb-peripheral/function-000/cdc-eth-function",

      // Power Device.
      "sys/platform/05:07:26/aml-power-impl-composite/power-impl",
      "sys/platform/05:07:26/aml-power-impl-composite/power-impl/composite-pd-armcore",
      "sys/platform/05:07:26/aml-power-impl-composite/power-impl/composite-pd-armcore/power-0",

      // Thermal
      "sys/platform/05:07:a/thermal",
      "class/thermal/000",
      "sys/platform/00:00:1b/sysmem",
      "sys/platform/00:00:e/tee/optee",

      // RAM (DDR) control.
      "sys/platform/05:07:24/ram",

      "sys/platform/05:07:1/aml-gpio/gpio-35/av400-buttons/hid-buttons",
      "sys/platform/05:07:1c/aml-nna",
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, GceArm64Test) {
  static const char* kDevicePaths[] = {
      // TODO(fxbug.dev/101529): Once we use userspace PCI, add PCI devices we expect to see.
      "sys/platform/pt/acpi",
      "sys/platform/pt/acpi/acpi-_SB_",
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, QemuArm64Test) {
  static const char* kDevicePaths[] = {
      "sys/platform/pt/qemu-bus",
      "sys/platform/00:00:6/rtc",
      "sys/platform/pci/00:00.0",
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, Vim3Test) {
  static const char* kDevicePaths[] = {
      "sys/platform/pt/vim3",
      "sys/platform/00:00:1b/sysmem",
      "sys/platform/05:06:1/aml-gpio",
      "sys/platform/05:06:14/clocks",
      "sys/platform/05:00:2/aml-i2c",
      "sys/platform/05:00:2/aml-i2c/i2c/i2c-0-81/rtc",
      "sys/platform/05:06:9/ethernet_mac/aml-ethernet/dwmac/dwmac/eth_phy/phy_null_device",
      "sys/platform/05:06:9/ethernet_mac/aml-ethernet/dwmac/dwmac/Designware-MAC/ethernet",
      "sys/platform/05:06:9/ethernet_mac/aml-ethernet",
      "sys/platform/05:00:7/aml_sd/aml-sd-emmc",
      "sys/platform/05:00:6/aml_sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-1",
      "sys/platform/05:00:6/aml_sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-2",
      "sys/platform/05:06:1c/aml-nna",
      "sys/platform/00:00:29",  // registers device
      "sys/platform/05:06:17/mali/aml-gpu",
      "sys/platform/05:00:10/aml-canvas",
      "sys/platform/05:06:d/display/amlogic-display/display-controller",
      "sys/platform/05:06:2b/aml-hdmi",
      "sys/platform/05:06:1d",  // pwm
      "sys/platform/05:06:1d/aml-pwm-device/pwm-9/vreg/pwm-0-regulator",
      "sys/platform/05:06:1d/aml-pwm-device/pwm-9/vreg/pwm-9-regulator",
      "sys/platform/05:06:26/aml-power-impl-composite",
      "sys/platform/05:06:26/aml-power-impl-composite/power-impl/pd-big-core",
      "sys/platform/05:06:26/aml-power-impl-composite/power-impl/pd-little-core",
      "sys/platform/05:06:26",  // power

      // CPU devices.
      "sys/platform/05:06:1e",
      "sys/platform/05:06:26/aml-power-impl-composite/power-impl/pd-big-core/power-0/aml-cpu/a311d-arm-a73",
      "sys/platform/05:06:26/aml-power-impl-composite/power-impl/pd-big-core/power-0/aml-cpu/a311d-arm-a53",

      "sys/platform/05:00:2/aml-i2c/i2c/i2c-0-34/fusb302",

      // USB
      "sys/platform/05:03:2d/vim3-usb-phy",
      "sys/platform/05:03:2d/vim3-usb-phy/vim3-usb-phy/dwc2/dwc2/dwc2/usb-peripheral/function-000/cdc-eth-function",
      "sys/platform/05:03:2d/vim3-usb-phy/vim3-usb-phy/xhci/xhci",
      // TODO(https://fxbug.dev/103458): usb-bus fails to bind occasionally. Temporarily disabling
      // testing for this device until the root cause is fixed.
      // USB 2.0 Hub
      // "sys/platform/05:03:2d/vim3-usb-phy/vim3-usb-phy/xhci/xhci/xhci/usb-bus/000/usb-hub",

      // Thermal
      "sys/platform/05:06:28",
      "sys/platform/05:06:a",
      "class/thermal/000",

      // GPIO
      "sys/platform/05:00:2/aml-i2c/i2c/i2c-0-32/gpio-expander/ti-tca6408a/gpio-107",

      "sys/platform/05:00:2/aml-i2c/i2c/i2c-0-24/vim3-mcu",
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, AstroTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/pt/astro",
      "sys/platform/05:03:1/aml-gpio",
      "sys/platform/05:03:1/aml-gpio/gpio-5/astro-buttons/hid-buttons",
      "sys/platform/05:00:2/aml-i2c",
      "sys/platform/05:03:17/mali/aml-gpu",
      "sys/platform/05:00:18/aml-usb-phy-v2",
      "sys/platform/05:00:3/bt-uart/aml-uart/bt-transport-uart",
      "sys/platform/05:00:3/bt-uart/aml-uart/bt-transport-uart/bt-hci-broadcom",

      // XHCI driver will not be loaded if we are in USB peripheral mode.
      // "xhci/xhci/usb-bus",

      "sys/platform/05:00:2/aml-i2c/i2c/i2c-2-44/backlight/ti-lp8556",
      "sys/platform/00:00:1e/dw-dsi/display/amlogic-display/display-controller",
      "sys/platform/00:00:1e/dw-dsi",
      "sys/platform/00:00:1e/dw-dsi/dw-dsi-base",
      "sys/platform/05:00:10/aml-canvas",
      "sys/platform/00:00:e/tee/optee",
      "sys/platform/05:03:e/aml-video",
      "sys/platform/05:00:f/aml-raw_nand/nand/bl2/skip-block",
      "sys/platform/05:00:f/aml-raw_nand/nand/tpl/skip-block",
      "sys/platform/05:00:f/aml-raw_nand/nand/fts/skip-block",
      "sys/platform/05:00:f/aml-raw_nand/nand/factory/skip-block",
      "sys/platform/05:00:f/aml-raw_nand/nand/zircon-b/skip-block",
      "sys/platform/05:00:f/aml-raw_nand/nand/zircon-a/skip-block",
      "sys/platform/05:00:f/aml-raw_nand/nand/zircon-r/skip-block",
      "sys/platform/05:00:f/aml-raw_nand/nand/sys-config/skip-block",
      "sys/platform/05:00:f/aml-raw_nand/nand/migration/skip-block",
      "sys/platform/05:00:7/aml-sdio/aml-sd-emmc/sdmmc",
      "sys/platform/05:00:7/aml-sdio/aml-sd-emmc/sdmmc/sdmmc-sdio",
      "sys/platform/05:00:7/aml-sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-1",
      "sys/platform/05:00:7/aml-sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-2",
      "sys/platform/05:00:7/aml-sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-1/wifi/brcmfmac-wlanphy",
      "sys/platform/05:00:7/aml-sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-1/wifi/brcmfmac-wlanphy/wlanphy",
      "sys/platform/05:00:2/aml-i2c/i2c/i2c-0-57/tcs3400-light/tcs-3400",
      "sys/platform/05:03:11/clocks",
      "sys/platform/05:03:12:1/astro-i2s-audio-out",
      "sys/platform/05:03:13/astro-audio-pdm-in",
      "sys/platform/05:03:1a/aml-secure-mem/aml-securemem",
      //"sys/platform/05:05:3/aml-uart/serial/bt-transport-uart/bcm-hci",
      "sys/platform/05:03:1d/aml-pwm-device/pwm-4/pwm-init",

      // CPU Device.
      "sys/platform/03:03:6",
      "class/cpu-ctrl/000",
      "sys/platform/03:03:26/aml-power-impl-composite/power-impl/composite-pd-armcore/power-0/aml-cpu/s905d2-arm-a53",
      // LED.
      "class/light/000",
      // RAM (DDR) control.
      "sys/platform/05:03:24/ram",

      // Power Device.
      "sys/platform/03:03:26/aml-power-impl-composite",
      "sys/platform/03:03:26/aml-power-impl-composite/power-impl/composite-pd-armcore",
      "sys/platform/03:03:26/aml-power-impl-composite/power-impl/composite-pd-armcore/power-0",

      // Thermal
      "sys/platform/05:03:a/thermal",
      "sys/platform/05:03:28/thermal",
      "class/thermal/000",
      "class/thermal/001",

      // Thermistor/ADC
      "class/adc/000",
      "class/adc/001",
      "class/adc/002",
      "class/adc/003",
      "class/temperature/000",
      "class/temperature/001",
      "class/temperature/002",
      "class/temperature/003",

      // Registers Device.
      "sys/platform/00:00:29",
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicePaths, std::size(kDevicePaths)));

  static const char* kTouchscreenDevicePaths[] = {
      "sys/platform/05:00:2/aml-i2c/i2c/i2c-1-56/ft3x27-touch/focaltouch HidDevice/hid-device/InputReport",
      "sys/platform/05:00:2/aml-i2c/i2c/i2c-1-93/gt92xx-touch/gt92xx HidDevice/hid-device/InputReport",
  };
  ASSERT_NO_FATAL_FAILURE(
      WaitForOne(cpp20::span(kTouchscreenDevicePaths, std::size(kTouchscreenDevicePaths))));
}

TEST_F(DeviceEnumerationTest, NelsonTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/pt/nelson",
      "sys/platform/05:05:1/aml-gpio",
      "sys/platform/05:05:1:1/aml-gpio",
      "sys/platform/05:05:1/aml-gpio/gpio-5/nelson-buttons/hid-buttons",
      "sys/platform/05:00:3/bt-uart/aml-uart/bt-transport-uart",
      "sys/platform/05:00:3/bt-uart/aml-uart/bt-transport-uart/bt-hci-broadcom",
      "sys/platform/05:00:2/aml-i2c",
      "sys/platform/05:05:17/mali/aml-gpu",
      "sys/platform/05:0a:21/nelson-usb-phy",
      "sys/platform/05:05:12/nelson-audio-i2s-out",
      "sys/platform/05:05:13/nelson-audio-pdm-in",
      "sys/platform/00:00:29",  // registers device

      // XHCI driver will not be loaded if we are in USB peripheral mode.
      // "xhci/xhci/usb-bus",

      "sys/platform/05:00:2/aml-i2c/i2c/i2c-2-44/backlight/ti-lp8556",
      "sys/platform/05:00:10/aml-canvas",
      "sys/platform/00:00:e/tee/optee",
      "sys/platform/05:00:8/nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/boot1/block",
      "sys/platform/05:00:8/nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/boot2/block",
      "sys/platform/05:00:8/nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/rpmb",
      "sys/platform/05:00:8/nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-000/block",
      "sys/platform/05:00:8/nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-001/block",
      "sys/platform/05:00:8/nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-002/block",
      "sys/platform/05:00:8/nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-003/block",
      "sys/platform/05:00:8/nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-004/block",
      "sys/platform/05:00:8/nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-005/block",
      "sys/platform/05:00:8/nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-006/block",
      "sys/platform/05:00:8/nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-007/block",
      "sys/platform/05:00:8/nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-008/block",
      "sys/platform/05:00:8/nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-009/block",
      "sys/platform/05:00:8/nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-010/block",
      "sys/platform/05:00:8/nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-011/block",
      "sys/platform/05:00:8/nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-012/block",
      "sys/platform/05:00:8/nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-013/block",
      "sys/platform/05:00:8/nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-014/block",
      "sys/platform/05:00:8/nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-015/block",
      "sys/platform/05:00:2/aml-i2c/i2c/i2c-0-57/tcs3400-light/tcs-3400",
      "sys/platform/05:05:1c/aml-nna",
      "sys/platform/05:05:22/clocks",
      "sys/platform/05:05:a/aml-thermal-pll/thermal",
      "class/thermal/000",
      // "sys/platform/05:03:1e/cpu",
      "sys/platform/05:03:1a/aml-secure-mem/aml-securemem",
      "class/pwm/000",
      "class/pwm/001",
      "class/pwm/002",
      "class/pwm/003",
      "class/pwm/004",
      "class/pwm/005",
      "class/pwm/006",
      "class/pwm/007",
      "class/pwm/008",
      "class/pwm/009",
      "sys/platform/05:00:6/aml-sdio/aml-sd-emmc/sdmmc",
      "sys/platform/05:00:6/aml-sdio/aml-sd-emmc/sdmmc/sdmmc-sdio",
      "sys/platform/05:00:6/aml-sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-1",
      "sys/platform/05:00:6/aml-sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-2",
      "sys/platform/05:00:6/aml-sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-1/wifi/brcmfmac-wlanphy",
      "sys/platform/05:00:6/aml-sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-1/wifi/brcmfmac-wlanphy/wlanphy",
      "sys/platform/00:00:1e/dw-dsi",
      "sys/platform/00:00:1e/dw-dsi/display/amlogic-display/display-controller",
      "sys/platform/00:00:1e/dw-dsi/dw-dsi-base",
      "sys/platform/05:00:2/aml-i2c/i2c/i2c-2-73/ti-ina231-mlb/ti-ina231",
      "sys/platform/05:00:2/aml-i2c/i2c/i2c-2-64/ti-ina231-speakers/ti-ina231",
      "sys/platform/05:00:2/aml-i2c/i2c/i2c-0-112/shtv3",
      "sys/platform/1c:00:1/gt6853-touch/gt6853",

      // Amber LED.
      "sys/platform/05:00:1c/gpio-light",
      "class/light/000",

      "sys/platform/05:05:1:1/aml-gpio/gpio-82/spi-1/aml-spi-1/spi/spi-1-0/spi-banjo-1-0/selina/selina",

      "sys/platform/05:05:24/ram",

      "sys/platform/03:0a:27/thermistor-device/therm-thread",
      "sys/platform/03:0a:27/thermistor-device/therm-audio",

      "sys/platform/05:00:2/aml-i2c/i2c/i2c-2-45/audio-tas58xx/TAS5805m/brownout-protection",

      "sys/platform/05:00:19/spi-0/aml-spi-0/spi/spi-0-0",
      "sys/platform/00:0a:23/nrf52811-radio",
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicePaths, std::size(kDevicePaths)));

  static const char* kTouchscreenDevicePaths[] = {
      // One of these touch devices could be on P0/P1 boards.
      "sys/platform/05:05:1/aml-gpio/gpio-5/nelson-buttons/hid-buttons/hidbus_function/hid-device/InputReport",
      // This is the only possible touch device for P2 and beyond.
      "sys/platform/1c:00:1/gt6853-touch/gt6853",
  };
  ASSERT_NO_FATAL_FAILURE(
      WaitForOne(cpp20::span(kTouchscreenDevicePaths, std::size(kTouchscreenDevicePaths))));
}

TEST_F(DeviceEnumerationTest, SherlockTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/pt/sherlock",
      "sys/platform/05:04:1/aml-gpio",
      "sys/platform/05:00:14/clocks",
      "sys/platform/05:00:2/aml-i2c",
      "sys/platform/05:00:10/aml-canvas",
      "sys/platform/05:04:a/aml-thermal-pll/thermal",
      "sys/platform/00:00:1e/dw-dsi",
      "sys/platform/00:00:1e/dw-dsi/display/amlogic-display/display-controller",
      "sys/platform/00:00:1e/dw-dsi/dw-dsi-base",
      "sys/platform/05:00:18/aml-usb-phy-v2",

      // XHCI driver will not be loaded if we are in USB peripheral mode.
      // "xhci/xhci/usb-bus",

      "sys/platform/05:00:8/sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/boot1/block",
      "sys/platform/05:00:8/sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/boot2/block",
      "sys/platform/05:00:8/sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/rpmb",
      "sys/platform/05:00:8/sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-000/block",
      "sys/platform/05:00:8/sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-002/block",
      "sys/platform/05:00:8/sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-000/block",
      "sys/platform/05:00:8/sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-002/block",
      "sys/platform/05:00:8/sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-003/block",
      "sys/platform/05:00:8/sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-004/block",
      "sys/platform/05:00:8/sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-005/block",
      "sys/platform/05:00:8/sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-006/block",
      "sys/platform/05:00:8/sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-007/block",
      "sys/platform/05:00:8/sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-008/block",
      "sys/platform/05:00:8/sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-009/block",
      "sys/platform/05:00:8/sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-010/block",
      "sys/platform/05:00:6/sherlock-sd-emmc/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-1",
      "sys/platform/05:00:6/sherlock-sd-emmc/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-2",
      "sys/platform/05:00:6/sherlock-sd-emmc/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-1/wifi/brcmfmac-wlanphy",
      "sys/platform/05:00:6/sherlock-sd-emmc/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-1/wifi/brcmfmac-wlanphy/wlanphy",
      "sys/platform/05:04:15/aml-mipi",
      "sys/platform/05:04:1c/aml-nna",
      "sys/platform/05:04:1d",  // pwm
      "sys/platform/05:04:15/aml-mipi/imx227-sensor/imx227/gdc",
      "sys/platform/05:04:15/aml-mipi/imx227-sensor/imx227/ge2d",
      "sys/platform/05:00:1c/gpio-light",
      "sys/platform/05:04:15/aml-mipi/imx227-sensor",
      "sys/platform/05:04:15/aml-mipi/imx227-sensor/imx227/isp",
      "sys/platform/05:04:15/aml-mipi/imx227-sensor/imx227/isp/arm-isp/camera-controller",
      "sys/platform/05:04:e/aml-video",
      "sys/platform/05:04:23/aml-video-enc",
      "sys/platform/05:04:25/aml-hevc-enc",
      "sys/platform/05:04:17/mali/aml-gpu",
      "sys/platform/05:04:13/sherlock-audio-pdm-in",
      "sys/platform/05:04:12:1/sherlock-i2s-audio-out",
      "sys/platform/05:00:2/aml-i2c/i2c/i2c-1-56/ft5726-touch",
      "sys/platform/00:00:e/tee/optee",
      "sys/platform/05:00:19/spi-0/aml-spi-0/spi/spi-0-0",
      "sys/platform/05:04:1/aml-gpio/gpio-4/sherlock-buttons/hid-buttons",
      "sys/platform/00:05:23/nrf52840-radio",
      "sys/platform/05:00:2/aml-i2c/i2c/i2c-2-44/backlight/ti-lp8556",
      "sys/platform/05:00:2/aml-i2c/i2c/i2c-0-57/SherlockLightSensor/tcs-3400",
      "sys/platform/05:04:1a/aml-secure-mem/aml-securemem",
      "sys/platform/05:04:1d/aml-pwm-device/pwm-4/pwm-init",
      "sys/platform/05:04:24/ram",
      "sys/platform/00:00:29",  // registers device

      // CPU Devices.
      "sys/platform/03:05:6",
      "class/cpu-ctrl/000",
      "class/cpu-ctrl/001",
      "sys/platform/05:04:a/aml-thermal-pll/thermal/aml-cpu/big-cluster",
      "sys/platform/05:04:a/aml-thermal-pll/thermal/aml-cpu/little-cluster",

      // Thermal devices.
      "sys/platform/05:04:a",
      "sys/platform/05:04:28",
      "class/thermal/000",
      "class/thermal/001",

      "class/adc/000",
      "class/adc/001",
      "class/adc/002",
      "class/adc/003",
      "class/temperature/000",
      "class/temperature/001",
      "class/temperature/002",

      // LCD Bias
      "sys/platform/05:00:2/aml-i2c/i2c/i2c-2-62",
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, LuisTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/05:04:13/luis-audio-pdm-in",
      "luis-i2s-audio-out",
      "sherlock-buttons/hid-buttons",

      // Thermal devices
      "sys/platform/05:04:28/thermal",

      // Thermistor and ADC devices
      "sys/platform/03:0c:27/thermistor-device/therm-mic",
      "sys/platform/03:0c:27/thermistor-device/therm-amp",
      "sys/platform/03:0c:27/thermistor-device/therm-ambient",
      "class/adc/000",
      "class/adc/001",
      "class/adc/002",
      "class/adc/003",
      "class/temperature/000",
      "class/temperature/001",
      "class/temperature/002",

      // Power Device Bucks.
      "0p8_ee_buck",
      "cpu_a_buck",

      // Power Implementation Device / Children.
      "aml-power-impl-composite",
      "composite-pd-big-core",
      "composite-pd-big-core/power-0",
      "composite-pd-little-core",
      "composite-pd-little-core/power-1",

      // CPU Device.
      // TODO(fxbug.dev/60492): Temporarily removed.
      // "sys/platform/03:0c:6",
      // "class/cpu-ctrl/000",
      // "class/cpu-ctrl/001",

      // USB ethernet; Can be RNDIS or CDC based on build config. Update this after fxbug.dev/58584
      // is fixed.
      "dwc2/dwc2/usb-peripheral/function-000",
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, EveTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/pci/00:1f.3/intel-hda-000/output-stream-001",     // Controller
                                                                      // headphones/speakers.
      "sys/platform/pci/00:1f.3/intel-hda-000/output-stream-003",     // Controller
                                                                      // headphones/speakers.
      "sys/platform/pci/00:1f.3/intel-hda-000/input-stream-002",      // Controller mics.
      "sys/platform/pci/00:19.2/i2c-bus-9d64/i2c/i2c-0-57/max98927",  // Codec left speaker.
      "sys/platform/pci/00:19.2/i2c-bus-9d64/i2c/i2c-0-58/max98927",  // Codec right speaker.
      "sys/platform/pci/00:19.2/i2c-bus-9d64/i2c/i2c-0-19/alc5663",   // Codec headphones.
      "sys/platform/pci/00:19.2/i2c-bus-9d64/i2c/i2c-0-87/alc5514",   // Codec mics.
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, NucTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/pt/PCI0/bus/00:02.0_/pci-00:02.0-fidl/intel_i915/intel-gpu-core",
      "sys/platform/pt/PCI0/bus/00:02.0_/pci-00:02.0-fidl/intel_i915/intel-display-controller/display-controller",
      "sys/platform/pt/PCI0/bus/00:14.0_/pci-00:14.0-fidl/xhci/usb-bus",
      "sys/platform/pt/PCI0/bus/00:15.0_/pci-00:15.0-fidl/i2c-bus-9d60",
      "sys/platform/pt/PCI0/bus/00:15.1_/pci-00:15.1-fidl/i2c-bus-9d61",
      "sys/platform/pt/PCI0/bus/00:17.0_/pci-00:17.0-fidl/ahci",
      // TODO(fxbug.dev/84037): Temporarily removed.
      // "pci-00:1f.3-fidl/intel-hda-000",
      // "pci-00:1f.3-fidl/intel-hda-controller",
      "sys/platform/pt/PCI0/bus/00:1f.6_/pci-00:1f.6-fidl/e1000",
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, VisaliaTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/14:01:1",
      "sys/platform/14:01:1/as370-gpio",
      "sys/platform/00:00:9",
      "sys/platform/00:00:9/dw-i2c",
      "sys/platform/14:01:2/as370-usb-phy",
      "sys/platform/14:01:a/as370-sdhci/sdhci/sdmmc/sdmmc-sdio/sdmmc-sdio-1",
      "sys/platform/14:01:a/as370-sdhci/sdhci/sdmmc/sdmmc-sdio/sdmmc-sdio-2",
      "dwc2-usb",
      "sys/platform/00:00:22/cadence-hpnfc/nand/fvm/ftl/block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/tzk_normal/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/tzk_normalB/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/bl_normal/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/bl_normalB/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/boot/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/recovery/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/fts/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/factory_store/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/key_1st/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/key_2nd/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/fastboot_1st/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/fastboot_2nd/skip-block",
      "power/as370-power",
      "power/as370-power/fragment-0",
      "class/thermal/000",
      "lp5018-light",
      "lp5018-light/lp50xx-light",
      "as370-touch",
      "as370-touch/cy8cmbr3108",
      "audio-max98373",
      "as370-audio-in",
      "as370-audio-out",
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicePaths, std::size(kDevicePaths)));
  EXPECT_EQ(zx_system_get_num_cpus(), 4);
}

TEST_F(DeviceEnumerationTest, PinecrestTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/14:01:1/as370-gpio",
      "sys/platform/00:00:9/dw-i2c",
      "sys/platform/14:01:2/as370-usb-phy",
      "sys/platform/14:01:a/as370-sdhci/sdhci/sdmmc/sdmmc-sdio/sdmmc-sdio-1",
      "sys/platform/14:01:a/as370-sdhci/sdhci/sdmmc/sdmmc-sdio/sdmmc-sdio-2",
      "sys/platform/14:01:11/pinecrest-emmc/as370-sdhci/sdhci/sdmmc/sdmmc-mmc/user/block/part-000",
      "sys/platform/14:01:11/pinecrest-emmc/as370-sdhci/sdhci/sdmmc/sdmmc-mmc/boot1/block",
      "sys/platform/14:01:11/pinecrest-emmc/as370-sdhci/sdhci/sdmmc/sdmmc-mmc/boot2/block",
      "sys/platform/14:01:11/pinecrest-emmc/as370-sdhci/sdhci/sdmmc/sdmmc-mmc/rpmb",
      "sys/platform/14:01:2/as370-usb-phy/dwc2/dwc2-usb/dwc2",
      "sys/platform/00:00:9/dw-i2c/i2c/i2c-0-102/power/as370-power",
      "sys/platform/14:00:8/thermal/as370-thermal",
      "sys/platform/10:02:5/lp5018-light/lp50xx-light",
      "sys/platform/00:00:9/dw-i2c/i2c/i2c-1-55/pinecrest-touch/cy8cmbr3108/hid-device/InputReport",
      "sys/platform/14:01:6/synaptics-dhub/pinecrest-audio-in/as370-audio-in",
      "sys/platform/14:01:6/synaptics-dhub/pinecrest-audio-out",
      "sys/platform/14:01:12/pinecrest-nna/as370-nna",
      "sys/platform/14:01:a/as370-sdhci/sdhci/sdmmc/sdmmc-sdio/sdmmc-sdio-1/wifi/nxpfmac_sdio-wlanphy",
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicePaths, std::size(kDevicePaths)));
  EXPECT_EQ(zx_system_get_num_cpus(), 4);
}

TEST_F(DeviceEnumerationTest, AtlasTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/pt/pci/00:19.2_/pci-00:19.2-fidl/i2c-bus-9d64/i2c/i2c-3-26",
      "sys/platform/pt/pci/01:00.0_/pci-01:00.0-fidl/iwlwifi-wlanphyimpl",
      // Codec headphones.
      "sys/platform/pt/acpi/acpi-_SB_/acpi-PCI0/acpi-I2C4/acpi-MAXL/pt/acpi-MAXL-composite/MAX98373",
      "sys/platform/pt/acpi/acpi-_SB_/acpi-PCI0/acpi-I2C4/acpi-MAXR/pt/acpi-MAXR-composite/MAX98373",
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicePaths, std::size(kDevicePaths)));

  if (IsDfv2Enabled()) {
    return;
  }

  // TODO(fxbug.dev/107847): Move this back to the normal kDevicePaths when wlanphy_dfv2 is
  // re-enabled.
  static const char* kDevicesThatFailInDfv2[] = {
      "sys/platform/pt/pci/01:00.0_/pci-01:00.0-fidl/iwlwifi-wlanphyimpl/wlanphy",
  };
  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicesThatFailInDfv2, std::size(kDevicesThatFailInDfv2)));
}

TEST_F(DeviceEnumerationTest, NocturneTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/pci/00:1f.3/intel-hda-000/input-stream-002",
      "sys/platform/pci/00:1f.3/intel-hda-000/output-stream-001",
      "sys/platform/pci/00:02.0/intel_i915/intel-gpu-core/msd-intel-gen",
      "sys/platform/pci/00:02.0/intel_i915/display-controller",
      "sys/platform/pt/acpi/TSR0",
      "sys/platform/pt/acpi/TSR1",
      "sys/platform/pt/acpi/TSR2",
      "sys/platform/pt/acpi/TSR3",
      "sys/platform/pt/acpi/acpi-lid/hid-device/InputReport",
      "sys/platform/pt/acpi/acpi-pwrbtn/hid-device/InputReport",
      "sys/platform/pci/00:15.0/i2c-bus-9d60/000a/i2c-hid/hid-device/InputReport",
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, QemuX64Q35Test) {
  static const char* kDevicePaths[] = {
      "sys/platform/00:00:1b/sysmem",

      "sys/platform/pt/acpi",
      "sys/platform/pt/acpi/acpi-pwrbtn",
      "sys/platform/pt/PCI0/bus/00:1f.2_/pci-00:1f.2-fidl/ahci",
      "sys/platform/pt/acpi/acpi-_SB_/acpi-PCI0/acpi-ISA_/acpi-KBD_/pt/acpi-KBD_-composite/i8042/i8042-keyboard",
      "sys/platform/pt/acpi/acpi-_SB_/acpi-PCI0/acpi-ISA_/acpi-KBD_/pt/acpi-KBD_-composite/i8042/i8042-mouse",
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicePaths, std::size(kDevicePaths)));

  if (!device_enumeration::IsAemuBoard()) {
    return;
  }
  printf("INFO: AEMU board detected. Test enumerating AEMU-specific devices.\n");

  static const char* kAemuDevicePaths[] = {
      "sys/platform/pt/PCI0/bus/00:01.0_/pci-00:01.0-fidl/virtio-input",
      "sys/platform/pt/PCI0/bus/00:02.0_/pci-00:02.0-fidl/virtio-input",
      "sys/platform/pt/PCI0/bus/00:0b.0_/pci-00:0b.0-fidl/goldfish-address-space",

      // Verify goldfish pipe root device created.
      "sys/platform/pt/acpi/acpi-_SB_/acpi-GFPP/pt/acpi-GFPP-composite/goldfish-pipe",
      // Verify goldfish pipe child devices created.
      "sys/platform/pt/acpi/acpi-_SB_/acpi-GFPP/pt/acpi-GFPP-composite/goldfish-pipe/goldfish-pipe-control",
      "sys/platform/pt/acpi/acpi-_SB_/acpi-GFPP/pt/acpi-GFPP-composite/goldfish-pipe/goldfish-pipe-sensor",
      "sys/platform/pt/acpi/acpi-_SB_/acpi-GFSK/pt/acpi-GFSK-composite/goldfish-sync",

      "sys/platform/pt/acpi/acpi-_SB_/acpi-GFPP/pt/acpi-GFPP-composite/goldfish-pipe/goldfish-pipe-control/goldfish-control-2/goldfish-control",
      "sys/platform/pt/acpi/acpi-_SB_/acpi-GFPP/pt/acpi-GFPP-composite/goldfish-pipe/goldfish-pipe-control/goldfish-control-2/goldfish-control/goldfish-display",
      "sys/platform/pt/acpi/acpi-_SB_/acpi-GFPP/pt/acpi-GFPP-composite/goldfish-pipe/goldfish-pipe-control/goldfish-control-2",
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kAemuDevicePaths, std::size(kAemuDevicePaths)));
}

// If this test fails, it indicates that the board driver set the board name incorrectly.
TEST_F(DeviceEnumerationTest, GenericShouldFailTest) {
  ASSERT_TRUE(false,
              "Board name was a generic board name, likely indicating that the board driver failed "
              "to find a real board name.");
}

}  // namespace

int main(int argc, char** argv) {
  fbl::Vector<fbl::String> errors;
  auto options = zxtest::Runner::Options::FromArgs(argc, argv, &errors);
  zxtest::LogSink* log_sink = zxtest::Runner::GetInstance()->mutable_reporter()->mutable_log_sink();

  if (!errors.is_empty()) {
    for (const auto& error : errors) {
      log_sink->Write("%s\n", error.c_str());
    }
    options.help = true;
  }

  options.filter = fbl::StringPrintf("%s:%s", GetTestFilter().c_str(), options.filter.c_str());

  // Errors will always set help to true.
  if (options.help) {
    zxtest::Runner::Options::Usage(argv[0], log_sink);
    return errors.is_empty();
  }

  if (options.list) {
    zxtest::Runner::GetInstance()->List(options);
    return 0;
  }

  return zxtest::Runner::GetInstance()->Run(options);
}
