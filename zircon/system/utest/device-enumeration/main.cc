// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <zircon/status.h>

#include <iterator>

#include <fbl/algorithm.h>
#include <fbl/span.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <zxtest/base/log-sink.h>
#include <zxtest/zxtest.h>

#include "src/lib/fsl/io/device_watcher.h"
#include "zircon/system/utest/device-enumeration/aemu.h"

namespace {

using devmgr_integration_test::RecursiveWaitForFile;

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

void WaitForOne(fbl::Span<const char*> device_paths) {
  async::Loop loop = async::Loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::vector<std::unique_ptr<fsl::DeviceWatcher>> watchers;
  auto callback = fit::function<void()>([&loop]() { loop.Shutdown(); });

  for (const char* path : device_paths) {
    RecursiveWaitFor(std::string("/dev/") + path, 4, &callback, &watchers);
  }

  loop.Run();
}

fbl::String GetTestFilter() {
  constexpr char kSysInfoPath[] = "/svc/fuchsia.sysinfo.SysInfo";
  fbl::unique_fd sysinfo(open(kSysInfoPath, O_RDONLY));
  if (!sysinfo) {
    return "Unknown";
  }

  zx::channel channel;
  if (fdio_get_service_handle(sysinfo.release(), channel.reset_and_get_address()) != ZX_OK) {
    return "Unknown";
  }

  char board_name[fuchsia_sysinfo_BOARD_NAME_LEN + 1];
  zx_status_t status;
  size_t actual_size;
  zx_status_t fidl_status = fuchsia_sysinfo_SysInfoGetBoardName(channel.get(), &status, board_name,
                                                                sizeof(board_name), &actual_size);
  if (fidl_status != ZX_OK || status != ZX_OK) {
    return "Unknown";
  }
  board_name[actual_size] = '\0';

  printf("Found board %s\n", board_name);

  if (!strcmp(board_name, "qemu")) {
    return "*QemuArm64*";
  } else if (!strcmp(board_name, "vim2")) {
    return "*Vim2*";
  } else if (!strcmp(board_name, "vim3")) {
    return "*Vim3*";
  } else if (!strcmp(board_name, "astro")) {
    return "*Astro*";
  } else if (!strcmp(board_name, "sherlock")) {
    return "*Sherlock*";
  } else if (!strcmp(board_name, "msm8x53-som")) {
    return "*Msm8x53Som*";
  } else if (!strcmp(board_name, "Nocturne")) {
    return "*Nocturne*";
  } else if (!strcmp(board_name, "nelson")) {
    return "*Nelson*";
  } else if (!strcmp(board_name, "luis")) {
    return "*Luis*";
  } else if (!strcmp(board_name, "Eve")) {
    return "*Eve*";
  } else if (!strcmp(board_name, "NUC7i5DNB")) {
    return "*Nuc*";
  } else if (!strcmp(board_name, "Standard PC (Q35 + ICH9, 2009)")) {
    // QEMU and AEMU with emulated Q35 boards have this board name.
    return "*QemuX64Q35*";
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
    }
  }
};

TEST_F(DeviceEnumerationTest, QemuArm64Test) {
  static const char* kDevicePaths[] = {
      "sys/platform/qemu-bus",
      "sys/platform/00:00:6/rtc",
      "sys/platform/pci/00:00.0",
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, Vim2Test) {
  static const char* kDevicePaths[] = {
      "sys/platform/vim", "sys/platform/00:00:1b/sysmem", "sys/platform/05:02:1/aml-gxl-gpio",
      "sys/platform/05:00:2/aml-i2c", "sys/platform/05:02:4/clocks",
      "sys/platform/05:00:10/aml-canvas",
      "aml_emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-000/block",
      "aml_emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-001/block",
      "aml_emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-002/block",
      "aml_emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-003/block",
      "aml_emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-004/block",
      "aml_emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-005/block",
      "aml_emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-006/block",
      // "sys/platform/05:00:3/aml-uart/serial/bt-transport-uart/bcm-hci",
      "sys/platform/05:00:2/aml-i2c/i2c/i2c-1-81/rtc", "sys/platform/00:00:2/xhci/usb-bus",
      "mali/aml-gpu", "dwmac/dwmac/eth_phy/phy_null_device", "dwmac/dwmac/Designware-MAC/ethernet",
      "display/vim2-display/display-controller", "vim-thermal/vim-thermal", "gpio-light/gpio-light",
      "wifi/brcmfmac-wlanphy", "aml-sdio/aml-sd-emmc/sdmmc",
      "aml-sdio/aml-sd-emmc/sdmmc/sdmmc-sdio", "aml-sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-1",
      "aml-sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-2", "sys/platform/05:02:b/aml-mailbox",
      "class/thermal/000", "aml-video",
      "sys/platform/00:00:29",  // registers device
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, Vim3Test) {
  static const char* kDevicePaths[] = {
      "sys/platform/vim3",
      "sys/platform/00:00:1b/sysmem",
      "sys/platform/05:06:1/aml-axg-gpio",
      "sys/platform/05:00:14/clocks",
      "sys/platform/05:00:2/aml-i2c",
      "sys/platform/05:00:2/aml-i2c/i2c/i2c-0-81/rtc",
      "dwmac/dwmac/eth_phy/phy_null_device",
      "dwmac/dwmac/Designware-MAC/ethernet",
      "aml_sd/aml-sd-emmc",
      "aml_sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-1",
      "aml_sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-2",
      "aml-nna",
      "sys/platform/00:00:29",  // registers device
      "aml-usb-phy-v2",
      "dwc2/dwc2/usb-peripheral/function-000/cdc-eth-function",
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

      // Thermal
      "sys/platform/05:06:28",
      "sys/platform/05:06:a",
      "class/thermal/000",
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, AstroTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/astro",
      "sys/platform/05:03:1/aml-axg-gpio",
      "astro-buttons/hid-buttons",
      "sys/platform/05:00:2/aml-i2c",
      "mali/aml-gpu",
      "aml-usb-phy-v2",
      "class/bt-transport/000",
      "class/bt-hci/000",

      // XHCI driver will not be loaded if we are in USB peripheral mode.
      // "xhci/xhci/usb-bus",

      // TODO(fxbug.dev/33871): Astro can have one of two possible touch screens
      // so we can't just test that one of them is bound. That is why the
      // following test is disabled.
      // "sys/platform/03:03:5/gt92xx HidDevice/hid-device-000",
      "backlight/ti-lp8556",
      "display/amlogic-display/display-controller",
      "sys/platform/00:00:1e/dw-dsi",
      "class/dsi-base/000",
      "sys/platform/05:00:10/aml-canvas",
      "tee/optee",
      "aml-video",
      "sys/platform/00:00:f/fallback-rtc",
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

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, std::size(kDevicePaths)));

  static const char* kTouchscreenDevicePaths[] = {
      "gt92xx-touch/gt92xx HidDevice/hid-device-000",
      "ft3x27-touch/focaltouch HidDevice/hid-device-000",
  };
  ASSERT_NO_FATAL_FAILURES(
      WaitForOne(fbl::Span(kTouchscreenDevicePaths, std::size(kTouchscreenDevicePaths))));
}

TEST_F(DeviceEnumerationTest, NelsonTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/nelson",
      "sys/platform/05:03:1/aml-axg-gpio",
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

      // TODO(fxbug.dev/33871): Nelson can have one of two possible touch screens
      // so we can't just test that one of them is bound. That is why the
      // following test is disabled.
      // "sys/platform/03:03:5/gt92xx HidDevice/hid-device-000",
      "backlight/ti-lp8556",
      "sys/platform/05:00:10/aml-canvas",
      "tee/optee",
      "sys/platform/00:00:f/fallback-rtc",
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

      "spi-1/aml-spi-1/spi/spi-1-0",
      "selina/selina",
      "class/radar/000",

      "sys/platform/05:05:24/ram",

      "sys/platform/03:0a:27/thermistor-device/therm-thread",
      "sys/platform/03:0a:27/thermistor-device/therm-audio",

      "brownout-protection/nelson-brownout-protection",
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, SherlockTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/sherlock",
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
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, std::size(kDevicePaths)));
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

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, std::size(kDevicePaths)));
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

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, NucTest) {
  static const char* kDevicePaths[] = {
      "pci-00:02.0/intel_i915/intel-gpu-core",
      "pci-00:02.0/intel_i915/display-controller",
      "pci-00:14.0/xhci/usb-bus",
      "pci-00:15.0/i2c-bus-9d60",
      "pci-00:15.1/i2c-bus-9d61",
      "pci-00:17.0/ahci",
      // TODO(fxbug.dev/84037): Temporarily removed.
      // "pci-00:1f.3/intel-hda-000",
      // "pci-00:1f.3/intel-hda-controller",
      "pci-00:1f.6/e1000",
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, NocturneTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/pci/00:1f.3/intel-hda-000/input-stream-002",
      "sys/platform/pci/00:1f.3/intel-hda-000/output-stream-001",
      "sys/platform/pci/00:02.0/intel_i915/intel-gpu-core/msd-intel-gen",
      "sys/platform/pci/00:02.0/intel_i915/display-controller",
      "sys/platform/acpi/TSR0",
      "sys/platform/acpi/TSR1",
      "sys/platform/acpi/TSR2",
      "sys/platform/acpi/TSR3",
      "sys/platform/acpi/acpi-lid/hid-device-000",
      "sys/platform/acpi/acpi-pwrbtn/hid-device-000",
      "sys/platform/pci/00:15.0/i2c-bus-9d60/000a/i2c-hid/hid-device-000",
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, QemuX64Q35Test) {
  static const char* kDevicePaths[] = {
      "class/sysmem/000",

      "pci-00:00.0",
      "pci-00:1f.2/ahci",

      "sys/platform/acpi",
      "sys/platform/acpi/acpi-pwrbtn",
      "sys/platform/acpi/acpi-_SB_/acpi-PCI0/acpi-ISA_/acpi-KBD_/i8042-keyboard",
      "sys/platform/acpi/acpi-_SB_/acpi-PCI0/acpi-ISA_/acpi-KBD_/i8042-mouse",
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, std::size(kDevicePaths)));

  if (!device_enumeration::IsAemuBoard()) {
    return;
  }
  printf("INFO: AEMU board detected. Test enumerating AEMU-specific devices.\n");

  static const char* kAemuDevicePaths[] = {
      "pci-00:01.0/virtio-input",
      "pci-00:02.0/virtio-input",
      "pci-00:0b.0/goldfish-address-space",

      "sys/platform/acpi/acpi-_SB_/acpi-GFPP",
      // Verify goldfish pipe root device created.
      "sys/platform/acpi/acpi-_SB_/acpi-GFPP/goldfish-pipe",
      // Verify goldfish pipe child devices created.
      "sys/platform/acpi/acpi-_SB_/acpi-GFPP/goldfish-pipe/goldfish-pipe-control",
      "sys/platform/acpi/acpi-_SB_/acpi-GFPP/goldfish-pipe/goldfish-pipe-sensor",
      "sys/platform/acpi/acpi-_SB_/acpi-GFSK",
      "sys/platform/acpi/acpi-_SB_/acpi-GFSK/goldfish-sync",

      "goldfish-control-2",
      "goldfish-control-2/goldfish-control",
      "goldfish-control-2/goldfish-control/goldfish-display",
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kAemuDevicePaths, std::size(kAemuDevicePaths)));
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
