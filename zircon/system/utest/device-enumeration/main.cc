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
#include <lib/fdio/cpp/caller.h>
#include <lib/stdcompat/span.h>
#include <lib/sys/component/cpp/service_client.h>
#include <stdlib.h>
#include <unistd.h>
#include <zircon/status.h>

#include <iostream>
#include <iterator>

#include <fbl/algorithm.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <sdk/lib/device-watcher/cpp/device-watcher.h>
#include <zxtest/base/log-sink.h>
#include <zxtest/zxtest.h>

#include "src/lib/fsl/io/device_watcher.h"
#include "zircon/system/utest/device-enumeration/aemu.h"

namespace {

using device_watcher::RecursiveWaitForFile;

bool IsDfv2Enabled() {
  zx::status driver_dev_res = component::Connect<fuchsia_driver_development::DriverDevelopment>();
  if (driver_dev_res.is_error()) {
    printf("Failed to connect to DriverDevelopment: %s", driver_dev_res.status_string());
    return false;
  }
  fidl::WireResult result = fidl::WireCall(driver_dev_res.value())->IsDfv2();
  if (!result.ok()) {
    printf("Failed to request if DFv2 is enabled: %s", result.status_string());
    return false;
  }

  return result->response;
}

// Asyncronously wait for a path to appear, and call `callback` when the path exists.
// The `watchers` array is needed because each directory in the path needs to allocate a
// DeviceWatcher, and they need to be stored somewhere that can be freed later.
void RecursiveWaitFor(std::string full_path, size_t slash_index, fit::function<void()>* callback,
                      std::vector<std::unique_ptr<fsl::DeviceWatcher>>* watchers) {
  if (slash_index == full_path.size()) {
    fprintf(stderr, "Found %s \n", full_path.c_str());
    (*callback)();
    return;
  }

  std::string dir_path = full_path.substr(0, slash_index);
  size_t next_slash = full_path.find_first_of("/", slash_index + 1);
  if (next_slash == std::string::npos) {
    next_slash = full_path.size();
  }
  std::string file_name = full_path.substr(slash_index + 1, next_slash - (slash_index + 1));

  watchers->push_back(fsl::DeviceWatcher::Create(
      dir_path,
      [file_name, full_path, next_slash, callback, watchers](int dir_fd, const std::string& name) {
        if (name.compare(file_name) == 0) {
          RecursiveWaitFor(full_path, next_slash, callback, watchers);
        }
      }));
}

void WaitForOne(cpp20::span<const char*> device_paths) {
  async::Loop loop = async::Loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::vector<std::unique_ptr<fsl::DeviceWatcher>> watchers;
  auto callback = fit::function<void()>([&loop]() { loop.Shutdown(); });

  for (const char* path : device_paths) {
    RecursiveWaitFor(std::string("/dev/") + path, 4, &callback, &watchers);
  }

  loop.Run();
}

fbl::String GetTestFilter() {
  zx::status sys_info = component::Connect<fuchsia_sysinfo::SysInfo>();
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
  }

  return "Unknown";
}

class DeviceEnumerationTest : public zxtest::Test {
 protected:
  void TestRunner(const char** device_paths, size_t paths_num) {
    fbl::unique_fd devfs_root(open("/dev", O_RDONLY));
    ASSERT_TRUE(devfs_root);

    fbl::unique_fd fd;
    for (size_t i = 0; i < paths_num; ++i) {
      // stderr helps diagnosibility, since stdout doesn't show up in test logs
      fprintf(stderr, "Checking %s\n", device_paths[i]);
      EXPECT_OK(RecursiveWaitForFile(devfs_root, device_paths[i], &fd), "%s", device_paths[i]);

      // Check that we connected to a device, not a directory.
      fdio_cpp::UnownedFdioCaller caller(fd);
      EXPECT_OK(fidl::WireCall(caller.borrow_as<fuchsia_device::Controller>())
                    ->GetTopologicalPath()
                    .status());
    }
  }
};

TEST_F(DeviceEnumerationTest, Av400Test) {
  static const char* kDevicePaths[] = {
      "sys/platform/platform-passthrough/av400",
      "sys/platform/05:07:1/aml-axg-gpio",
      "sys/platform/05:07:1d",  // pwm
      "sys/platform/05:07:2c/clocks",
      "sys/platform/05:00:2/aml-i2c",
      "sys/platform/00:00:29",  // registers device
      "aml_emmc/aml-sd-emmc/sdmmc/sdmmc-mmc",
      "aml_sdio/aml-sd-emmc/sdmmc/sdmmc-sdio",
      "spi-1/aml-spi-1/spi/spi-1-0",
      "pwm-init",
      "dwmac/dwmac/eth_phy/phy_null_device",
      "dwmac/dwmac/Designware-MAC/ethernet",
      "ethernet_mac/aml-ethernet",
      "sys/platform/05:07:2e/aml-rtc",
      "av400-i2s-audio-out",
      "av400-i2s-audio-in",
      "sys/platform/05:07:13/av400-audio-pdm-in",
      "sys/platform/05:07:b/aml-mailbox",
      "dsp/aml-dsp",

      // CPU Device
      "sys/platform/05:07:1e",
      "class/cpu-ctrl/000",
      "aml-cpu/a5-arm-a55",

      // USB
      "aml-usb-crg-phy-v2",
      // Force to usb peripheral
      "udc/udc/usb-peripheral/function-000/cdc-eth-function",

      // Power Device.
      "aml-power-impl-composite/power-impl",
      "composite-pd-armcore",
      "composite-pd-armcore/power-0",

      // Thermal
      "sys/platform/05:07:a/thermal",
      "class/thermal/000",
      "sys/platform/00:00:1b/sysmem",
      "tee/optee",

      // RAM (DDR) control.
      "sys/platform/05:07:24/ram",

      "av400-buttons/hid-buttons",
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, GceArm64Test) {
  static const char* kDevicePaths[] = {
      // TODO(fxbug.dev/101529): Once we use userspace PCI, add PCI devices we expect to see.
      "sys/platform/platform-passthrough/acpi",
      "sys/platform/platform-passthrough/acpi/acpi-_SB_",
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, QemuArm64Test) {
  static const char* kDevicePaths[] = {
      "sys/platform/platform-passthrough/qemu-bus",
      "sys/platform/00:00:6/rtc",
      "sys/platform/pci/00:00.0",
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, Vim3Test) {
  static const char* kDevicePaths[] = {
      "sys/platform/platform-passthrough/vim3",
      "sys/platform/00:00:1b/sysmem",
      "sys/platform/05:06:1/aml-axg-gpio",
      "sys/platform/05:06:14/clocks",
      "sys/platform/05:00:2/aml-i2c",
      "sys/platform/05:00:2/aml-i2c/i2c/i2c-0-81/rtc",
      "dwmac/dwmac/eth_phy/phy_null_device",
      "dwmac/dwmac/Designware-MAC/ethernet",
      "ethernet_mac/aml-ethernet",
      "aml_sd/aml-sd-emmc",
      "aml_sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-1",
      "aml_sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-2",
      "aml-nna",
      "sys/platform/00:00:29",  // registers device
      "mali/aml-gpu",
      "sys/platform/05:00:10/aml-canvas",
      "display/amlogic-display/display-controller",
      "sys/platform/05:06:2b/aml-hdmi",
      "sys/platform/05:06:1d",  // pwm
      "vreg/pwm-0-regulator",
      "vreg/pwm-9-regulator",
      "aml-power-impl-composite",
      "pd-big-core",
      "pd-little-core",
      "sys/platform/05:06:26",  // power

      // CPU devices.
      "sys/platform/05:06:1e",
      "aml-cpu/a311d-arm-a73",
      "aml-cpu/a311d-arm-a53",

      "fusb302",

      // USB
      "vim3-usb-phy",
      "dwc2/dwc2/usb-peripheral/function-000/cdc-eth-function",
      "xhci/xhci",
      // TODO(https://fxbug.dev/103458): usb-bus fails to bind occasionally. Temporarily disabling
      // testing for this device until the root cause is fixed.
      // "xhci/xhci/usb-bus/000/usb-hub",  // USB 2.0 Hub

      // Thermal
      "sys/platform/05:06:28",
      "sys/platform/05:06:a",
      "class/thermal/000",

      // GPIO
      "gpio-expander/ti-tca6408a/gpio-107",

      "sys/platform/05:00:2/aml-i2c/i2c/i2c-0-24/vim3-mcu",
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, AstroTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/platform-passthrough/astro",
      "sys/platform/05:03:1/aml-axg-gpio",
      "astro-buttons/hid-buttons",
      "sys/platform/05:00:2/aml-i2c",
      "mali/aml-gpu",
      "aml-usb-phy-v2",
      "class/bt-transport/000",
      "class/bt-hci/000",

      // XHCI driver will not be loaded if we are in USB peripheral mode.
      // "xhci/xhci/usb-bus",

      "backlight/ti-lp8556",
      "display/amlogic-display/display-controller",
      "sys/platform/00:00:1e/dw-dsi",
      "class/dsi-base/000",
      "sys/platform/05:00:10/aml-canvas",
      "tee/optee",
      "aml-video",
      "sys/platform/05:00:f/aml-raw_nand/nand/bl2/skip-block",
      "sys/platform/05:00:f/aml-raw_nand/nand/tpl/skip-block",
      "sys/platform/05:00:f/aml-raw_nand/nand/fts/skip-block",
      "sys/platform/05:00:f/aml-raw_nand/nand/factory/skip-block",
      "sys/platform/05:00:f/aml-raw_nand/nand/zircon-b/skip-block",
      "sys/platform/05:00:f/aml-raw_nand/nand/zircon-a/skip-block",
      "sys/platform/05:00:f/aml-raw_nand/nand/zircon-r/skip-block",
      "sys/platform/05:00:f/aml-raw_nand/nand/sys-config/skip-block",
      "sys/platform/05:00:f/aml-raw_nand/nand/migration/skip-block",
      "aml-sdio/aml-sd-emmc/sdmmc",
      "aml-sdio/aml-sd-emmc/sdmmc/sdmmc-sdio",
      "aml-sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-1",
      "aml-sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-2",
      "wifi/brcmfmac-wlanphy",
      "wifi/brcmfmac-wlanphy/wlanphy",
      "tcs3400-light/tcs-3400",
      "sys/platform/05:03:11/clocks",
      "astro-i2s-audio-out",
      "sys/platform/05:03:13/astro-audio-pdm-in",
      "aml-secure-mem/aml-securemem",
      //"sys/platform/05:05:3/aml-uart/serial/bt-transport-uart/bcm-hci",
      "pwm-init",

      // CPU Device.
      "sys/platform/03:03:6",
      "class/cpu-ctrl/000",
      "aml-cpu/s905d2-arm-a53",
      // LED.
      "class/light/000",
      // RAM (DDR) control.
      "sys/platform/05:03:24/ram",

      // Power Device.
      "aml-power-impl-composite",
      "composite-pd-armcore",
      "composite-pd-armcore/power-0",

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
      "gt92xx-touch/gt92xx HidDevice/hid-device/InputReport",
      "ft3x27-touch/focaltouch HidDevice/hid-device/InputReport",
  };
  ASSERT_NO_FATAL_FAILURE(
      WaitForOne(cpp20::span(kTouchscreenDevicePaths, std::size(kTouchscreenDevicePaths))));
}

TEST_F(DeviceEnumerationTest, NelsonTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/platform-passthrough/nelson",
      "sys/platform/05:05:1/aml-axg-gpio",
      "sys/platform/05:05:1:1/aml-axg-gpio",
      "nelson-buttons/hid-buttons",
      "class/bt-transport/000",
      "class/bt-hci/000",
      "sys/platform/05:00:2/aml-i2c",
      "mali/aml-gpu",
      "sys/platform/05:0a:21/nelson-usb-phy",
      "nelson-audio-i2s-out",
      "sys/platform/05:05:13/nelson-audio-pdm-in",
      "sys/platform/00:00:29",  // registers device

      // XHCI driver will not be loaded if we are in USB peripheral mode.
      // "xhci/xhci/usb-bus",

      "backlight/ti-lp8556",
      "sys/platform/05:00:10/aml-canvas",
      "tee/optee",
      "nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/boot1/block",
      "nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/boot2/block",
      "nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/rpmb",
      "nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-000/block",
      "nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-001/block",
      "nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-002/block",
      "nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-003/block",
      "nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-004/block",
      "nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-005/block",
      "nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-006/block",
      "nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-007/block",
      "nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-008/block",
      "nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-009/block",
      "nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-010/block",
      "nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-011/block",
      "nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-012/block",
      "nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-013/block",
      "nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-014/block",
      "nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-015/block",
      "tcs3400-light/tcs-3400",
      "aml-nna",
      "sys/platform/05:05:22/clocks",
      "aml-thermal-pll/thermal",
      "class/thermal/000",
      // "sys/platform/05:03:1e/cpu",
      "aml-secure-mem/aml-securemem",
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
      "aml-sdio/aml-sd-emmc/sdmmc",
      "aml-sdio/aml-sd-emmc/sdmmc/sdmmc-sdio",
      "aml-sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-1",
      "aml-sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-2",
      "wifi/brcmfmac-wlanphy",
      "wifi/brcmfmac-wlanphy/wlanphy",
      "sys/platform/00:00:1e/dw-dsi",
      "display/amlogic-display/display-controller",
      "class/dsi-base/000",
      "ti-ina231-mlb/ti-ina231",
      "ti-ina231-speakers/ti-ina231",
      "sys/platform/05:00:2/aml-i2c/i2c/i2c-0-112/shtv3",
      "gt6853-touch/gt6853",

      // Amber LED.
      "gpio-light",
      "class/light/000",

      // This should exist, but open() will fail because it is already being used by radar.
      // "spi-1/aml-spi-1/spi/spi-1-0",
      "selina/selina",
      "class/radar/000",

      "sys/platform/05:05:24/ram",

      "sys/platform/03:0a:27/thermistor-device/therm-thread",
      "sys/platform/03:0a:27/thermistor-device/therm-audio",

      "brownout-protection/nelson-brownout-protection",

      "spi-0/aml-spi-0/spi/spi-0-0",
      "nrf52811-radio",
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicePaths, std::size(kDevicePaths)));

  static const char* kTouchscreenDevicePaths[] = {
      // One of these touch devices could be on P0/P1 boards.
      "gtx8x-touch/gt92xx HidDevice/hid-device/InputReport",
      "ft3x27-touch/focaltouch HidDevice/hid-device/InputReport",
      // This is the only possible touch device for P2 and beyond.
      "gt6853-touch/gt6853",
  };
  ASSERT_NO_FATAL_FAILURE(
      WaitForOne(cpp20::span(kTouchscreenDevicePaths, std::size(kTouchscreenDevicePaths))));
}

TEST_F(DeviceEnumerationTest, SherlockTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/platform-passthrough/sherlock",
      "sys/platform/05:04:1/aml-axg-gpio",
      "sys/platform/05:00:14/clocks",
      "sys/platform/05:00:2/aml-i2c",
      "sys/platform/05:00:10/aml-canvas",
      "aml-thermal-pll/thermal",
      "sys/platform/00:00:1e/dw-dsi",
      "display/amlogic-display/display-controller",
      "class/dsi-base/000",
      "aml-usb-phy-v2",

      // XHCI driver will not be loaded if we are in USB peripheral mode.
      // "xhci/xhci/usb-bus",

      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/boot1/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/boot2/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/rpmb",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-000/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-002/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-000/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-002/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-003/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-004/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-005/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-006/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-007/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-008/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-009/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-010/block",
      "sherlock-sd-emmc/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-1",
      "sherlock-sd-emmc/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-2",
      "wifi/brcmfmac-wlanphy",
      "wifi/brcmfmac-wlanphy/wlanphy",
      "sys/platform/05:04:15/aml-mipi",
      "aml-nna",
      "sys/platform/05:04:1d",  // pwm
      "gdc",
      "ge2d",
      "gpio-light",
      "imx227-sensor",
      "isp",
      "camera-controller",
      "aml-video",
      "aml-video-enc",
      "aml-hevc-enc",
      "mali/aml-gpu",
      "sys/platform/05:04:13/sherlock-audio-pdm-in",
      "sherlock-i2s-audio-out",
      "ft5726-touch",
      "tee/optee",
      "spi-0/aml-spi-0/spi/spi-0-0",
      "sherlock-buttons/hid-buttons",
      "nrf52840-radio",
      "backlight/ti-lp8556",
      "SherlockLightSensor/tcs-3400",
      "aml-secure-mem/aml-securemem",
      "pwm-init",
      "sys/platform/05:04:24/ram",
      "sys/platform/00:00:29",  // registers device

      // CPU Devices.
      "sys/platform/03:05:6",
      "class/cpu-ctrl/000",
      "class/cpu-ctrl/001",
      "aml-cpu/big-cluster",
      "aml-cpu/little-cluster",

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
      "pci-00:02.0-fidl/intel_i915/intel-gpu-core",
      "pci-00:02.0-fidl/intel_i915/intel-display-controller/display-controller",
      "pci-00:14.0-fidl/xhci/usb-bus",
      "pci-00:15.0-fidl/i2c-bus-9d60",
      "pci-00:15.1-fidl/i2c-bus-9d61",
      "pci-00:17.0-fidl/ahci",
      // TODO(fxbug.dev/84037): Temporarily removed.
      // "pci-00:1f.3-fidl/intel-hda-000",
      // "pci-00:1f.3-fidl/intel-hda-controller",
      "pci-00:1f.6-fidl/e1000",
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

TEST_F(DeviceEnumerationTest, AtlasTest) {
  static const char* kDevicePaths[] = {
      "pci-01:00.0-fidl/iwlwifi-wlanphyimpl",
      "acpi-MAXL-composite/MAX98373",  // Codec left speaker.
      "acpi-MAXR-composite/MAX98373",  // Codec right speaker.
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicePaths, std::size(kDevicePaths)));

  if (IsDfv2Enabled()) {
    return;
  }

  // TODO(fxbug.dev/106517): Fix these devices and move them back.
  static const char* kDevicesThatFailInDfv2[] = {
      "pci-00:19.2-fidl/i2c-bus-9d64/i2c/i2c-3-26",  // Codec headphones.
      "pci-01:00.0/iwlwifi-wlanphyimpl/wlanphy",
  };
  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicesThatFailInDfv2, std::size(kDevicesThatFailInDfv2)));
}

TEST_F(DeviceEnumerationTest, NocturneTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/pci/00:1f.3/intel-hda-000/input-stream-002",
      "sys/platform/pci/00:1f.3/intel-hda-000/output-stream-001",
      "sys/platform/pci/00:02.0/intel_i915/intel-gpu-core/msd-intel-gen",
      "sys/platform/pci/00:02.0/intel_i915/display-controller",
      "sys/platform/platform-passthrough/acpi/TSR0",
      "sys/platform/platform-passthrough/acpi/TSR1",
      "sys/platform/platform-passthrough/acpi/TSR2",
      "sys/platform/platform-passthrough/acpi/TSR3",
      "sys/platform/platform-passthrough/acpi/acpi-lid/hid-device/InputReport",
      "sys/platform/platform-passthrough/acpi/acpi-pwrbtn/hid-device/InputReport",
      "sys/platform/pci/00:15.0/i2c-bus-9d60/000a/i2c-hid/hid-device/InputReport",
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, QemuX64Q35Test) {
  static const char* kDevicePaths[] = {
      "class/sysmem/000",

      "pci-00:1f.2-fidl/ahci",

      "sys/platform/platform-passthrough/acpi",
      "sys/platform/platform-passthrough/acpi/acpi-pwrbtn",
      "acpi-KBD_-composite/i8042/i8042-keyboard",
      "acpi-KBD_-composite/i8042/i8042-mouse",
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicePaths, std::size(kDevicePaths)));

  if (!device_enumeration::IsAemuBoard()) {
    return;
  }
  printf("INFO: AEMU board detected. Test enumerating AEMU-specific devices.\n");

  static const char* kAemuDevicePaths[] = {
      "pci-00:01.0-fidl/virtio-input",
      "pci-00:02.0-fidl/virtio-input",
      "pci-00:0b.0-fidl/goldfish-address-space",

      // Verify goldfish pipe root device created.
      "acpi-GFPP-composite/goldfish-pipe",
      // Verify goldfish pipe child devices created.
      "acpi-GFPP-composite/goldfish-pipe/goldfish-pipe-control",
      "acpi-GFPP-composite/goldfish-pipe/goldfish-pipe-sensor",
      "acpi-GFSK-composite/goldfish-sync",

      "goldfish-control-2/goldfish-control",
      "goldfish-control-2/goldfish-control/goldfish-display",
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kAemuDevicePaths, std::size(kAemuDevicePaths)));

  if (IsDfv2Enabled()) {
    return;
  }

  // TODO(fxbug.dev/106517): Fix these devices and move them back.
  static const char* kDevicesThatFailInDfv2[] = {
      "acpi-GFPP-composite",
      "acpi-GFSK-composite",
      "goldfish-control-2",
  };

  ASSERT_NO_FATAL_FAILURE(TestRunner(kDevicesThatFailInDfv2, std::size(kDevicesThatFailInDfv2)));
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
