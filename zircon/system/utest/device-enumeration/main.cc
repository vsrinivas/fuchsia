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
  } else if (!strcmp(board_name, "cleo")) {
    return "*Cleo*";
  } else if (!strcmp(board_name, "sherlock")) {
    return "*Sherlock*";
  } else if (!strcmp(board_name, "mt8167s_ref")) {
    return "*Mt8167sRef*";
  } else if (!strcmp(board_name, "msm8x53-som")) {
    return "*Msm8x53Som*";
  } else if (!strcmp(board_name, "as370")) {
    return "*As370*";
  } else if (!strcmp(board_name, "visalia")) {
    return "*Visalia*";
  } else if (!strcmp(board_name, "Nocturne")) {
    return "*Nocturne*";
  } else if (!strcmp(board_name, "c18")) {
    return "*C18*";
  } else if (!strcmp(board_name, "nelson")) {
    return "*Nelson*";
  } else if (!strcmp(board_name, "vs680-evk")) {
    return "*Vs680Evk*";
  } else if (!strcmp(board_name, "luis")) {
    return "*Luis*";
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
      "sys/pci/00:00.0",
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, Vim2Test) {
  static const char* kDevicePaths[] = {
      "sys/platform/vim",
      "sys/platform/00:00:1b/sysmem",
      "sys/platform/05:02:1/aml-gxl-gpio",
      "sys/platform/05:00:2/aml-i2c",
      "sys/platform/05:02:4/clocks",
      "sys/platform/05:00:10/aml-canvas",
      "aml_emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-000/block",
      "aml_emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-001/block",
      "aml_emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-002/block",
      "aml_emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-003/block",
      "aml_emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-004/block",
      "aml_emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-005/block",
      "aml_emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-006/block",
      // "sys/platform/05:00:3/aml-uart/serial/bt-transport-uart/bcm-hci",
      "sys/platform/05:00:2/aml-i2c/i2c/i2c-1-81/rtc",
      "sys/platform/00:00:2/xhci/usb-bus",
      "sys/platform/05:02:17/aml-gpu",
      "dwmac/eth_phy/phy_null_device",
      "dwmac/Designware MAC/ethernet",
      "display/vim2-display/display-controller",
      "vim-thermal/vim-thermal",
      "gpio-light/gpio-light",
      "wifi/brcmfmac-wlanphy",
      "aml-sdio/aml-sd-emmc/sdmmc",
      "aml-sdio/aml-sd-emmc/sdmmc/sdmmc-sdio",
      "aml-sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-1",
      "aml-sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-2",
      "sys/platform/05:02:b/aml-mailbox",
      "class/thermal/000",
      "aml-video",
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
      "dwmac/eth_phy/phy_null_device",
      "dwmac/Designware MAC/ethernet",
      "aml_sd/aml-sd-emmc",
      "aml_sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-1",
      "aml_sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-2",
      "sys/platform/05:06:1c/aml-nna",
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, AstroTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/astro",
      "sys/platform/05:03:1/aml-axg-gpio",
      "astro-buttons/hid-buttons",
      "sys/platform/05:00:2/aml-i2c",
      "sys/platform/05:03:17/aml-gpu",
      "sys/platform/05:00:18/aml-usb-phy-v2",
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
      "tcs3400-light/tcs-3400/hid-device-000",
      "sys/platform/05:03:11/clocks",
      "sys/platform/05:03:a/thermal",
      "astro-i2s-audio-out",
      "sys/platform/05:03:13/astro-audio-in",
      "aml-secure-mem/aml-securemem",
      //"sys/platform/05:05:3/aml-uart/serial/bt-transport-uart/bcm-hci",
      "pwm-init",

      // CPU Device.
      "sys/platform/03:03:6",
      "class/cpu-ctrl/000",
      // LED.
      "class/light/000",
      // RAM (DDR) control.
      "sys/platform/05:03:24/ram",

      // Power Device.
      "aml-power-impl-composite",
      "composite-pd-armcore",
      "composite-pd-armcore/power-0",

      // Thermistor/ADC
      "class/adc/000",
      "class/adc/001",
      "class/adc/002",
      "class/adc/003",
      "class/temperature/000",
      "class/temperature/001",
      "class/temperature/002",
      "class/temperature/003",
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
      "sys/platform/05:03:17/aml-gpu",
      "sys/platform/05:00:21/nelson-usb-phy",
      "nelson-audio-out",
      "nelson-audio-in",

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
      "nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-016/block",
      "nelson-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-017/block",
      "tcs3400-light/tcs-3400/hid-device-000",
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
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, CleoTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/mt8167s_ref",
      "sys/platform/0d:00:1/mt8167-gpio",
      "sys/platform/0d:00:8/mtk-clk",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/boot1/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/boot2/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-000/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-001/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-002/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-003/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-004/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-005/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-006/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-007/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-008/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-009/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-010/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-011/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-012/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-013/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-014/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-015/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-016/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-017/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-018/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-019/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-020/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-021/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-022/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-023/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-024/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-025/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-026/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-027/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-028/block",
      "sdio/mtk-sdmmc/sdmmc/sdmmc-sdio/sdmmc-sdio-1",
      "sdio/mtk-sdmmc/sdmmc/sdmmc-sdio/sdmmc-sdio-2/bt-hci-mediatek",
      "class/bt-transport/000",
      "mt8167-buttons/hid-buttons",
      "sys/platform/0d:00:14/mt-usb/usb-peripheral/function-000/cdc-eth-function/ethernet",
      "touch/focaltouch HidDevice/hid-device-000",
      "thermal/mtk-thermal",
      "mt8167-i2c",
      "mt8167s_gpu/mt8167s-gpu",
      "sys/platform/00:00:f/fallback-rtc",
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
      "sys/platform/05:00:18/aml-usb-phy-v2",

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
      "sys/platform/05:04:1c/aml-nna",
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
      "sys/platform/05:04:17/aml-gpu",
      "sys/platform/05:04:16/sherlock-audio-in",
      "sherlock-i2s-audio-out",
      "ft5726-touch",
      "tee/optee",
      "sys/platform/00:00:f/fallback-rtc",
      "spi/aml-spi-0/spi/spi-0-0",
      "sherlock-buttons/hid-buttons",
      "nrf52840-radio",
      "backlight/ti-lp8556",
      "SherlockLightSensor/tcs-3400/hid-device-000",
      "aml-secure-mem/aml-securemem",
      "pwm-init",
      "sys/platform/05:04:24/ram",

      // CPU Device.
      "sys/platform/03:05:6",
      "class/cpu-ctrl/000",
      "class/cpu-ctrl/001",
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, LuisTest) {
  static const char* kDevicePaths[] = {
      "imx355-sensor",
      "sys/platform/05:04:16/sherlock-audio-in",
      "luis-i2s-audio-out",
      "sherlock-buttons/hid-buttons",
      "ft8201-touch",

      // Thermal devices
      "sys/platform/05:04:a/thermal",
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
      "sys/platform/03:0c:6",
      "class/cpu-ctrl/000",
      "class/cpu-ctrl/001",

      // USB ethernet; Can be RNDIS or CDC based on build config. Update this after fxb/58584 is
      // fixed.
      "dwc2/dwc2/usb-peripheral/function-000",
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, Mt8167sRefTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/mt8167s_ref",
      "sys/platform/0d:00:1/mt8167-gpio",
      "sys/platform/0d:00:6/mt8167-i2c",
      "sys/platform/0d:00:8/mtk-clk",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/boot1/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/boot2/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-000/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-001/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-002/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-003/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-004/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-005/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-006/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-007/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-008/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-009/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-010/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-011/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-012/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-013/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-014/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-015/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-016/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-017/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-018/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-019/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-020/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-021/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-022/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-023/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-024/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-025/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-026/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-027/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-028/block",
      "sdio/mtk-sdmmc/sdmmc/sdmmc-sdio/sdmmc-sdio-1",
      "sdio/mtk-sdmmc/sdmmc/sdmmc-sdio/sdmmc-sdio-2",
      "mt8167-buttons/hid-buttons",
      "sys/platform/0d:00:14/mt-usb/usb-peripheral/function-000/cdc-eth-function/ethernet",
      "sys/platform/0d:00:9/mtk-thermal",
      "sys/platform/00:00:f/fallback-rtc",
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, Msm8x53SomTest) {
  static const char* kDevicePaths[] = {"sys/platform/msm8x53",
                                       "sys/platform/13:01:1",
                                       "sys/platform/13:00:3/msm8x53-sdhci",
                                       "sys/platform/13:00:2/qcom-pil",
                                       "sys/platform/13:01:4/msm-clk",
                                       "sys/platform/13:01:5/msm8x53-power"};

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, As370Test) {
  static const char* kDevicePaths[] = {
      "sys/platform/as370",
      "sys/platform/14:01:1",
      "sys/platform/14:01:1/as370-gpio",
      "sys/platform/00:00:9",
      "sys/platform/00:00:9/dw-i2c",
      "sys/platform/14:01:2/as370-usb-phy",
      "sys/platform/14:01:a/as370-sdhci/sdhci/sdmmc/sdmmc-sdio/sdmmc-sdio-1",
      "dwc2-usb",
      "audio-max98373",
      "as370-audio-out",
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
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, std::size(kDevicePaths)));
  EXPECT_EQ(zx_system_get_num_cpus(), 4);
}

TEST_F(DeviceEnumerationTest, Vs680EvkTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/vs680-evk",
      "sys/platform/14:02:1/as370-gpio",
      "sys/platform/14:02:b/vs680-sdhci/sdhci/sdmmc/sdmmc-mmc/boot1/block",
      "sys/platform/14:02:b/vs680-sdhci/sdhci/sdmmc/sdmmc-mmc/boot2/block",
      "sys/platform/14:02:b/vs680-sdhci/sdhci/sdmmc/sdmmc-mmc/rpmb",
      "sys/platform/14:02:b/vs680-sdhci/sdhci/sdmmc/sdmmc-mmc/user/block/part-000/block",
      "sys/platform/14:02:b/vs680-sdhci/sdhci/sdmmc/sdmmc-mmc/user/block/part-001/block",
      "sys/platform/14:02:b/vs680-sdhci/sdhci/sdmmc/sdmmc-mmc/user/block/part-002/block",
      "sys/platform/14:02:b/vs680-sdhci/sdhci/sdmmc/sdmmc-mmc/user/block/part-003/block",
      "sys/platform/14:02:b/vs680-sdhci/sdhci/sdmmc/sdmmc-mmc/user/block/part-004/block",
      "sys/platform/14:02:b/vs680-sdhci/sdhci/sdmmc/sdmmc-mmc/user/block/part-005/block",
      "sys/platform/14:02:b/vs680-sdhci/sdhci/sdmmc/sdmmc-mmc/user/block/part-006/block",
      "sys/platform/14:02:b/vs680-sdhci/sdhci/sdmmc/sdmmc-mmc/user/block/part-007/block",
      "sys/platform/14:02:b/vs680-sdhci/sdhci/sdmmc/sdmmc-mmc/user/block/part-008/block",
      "sys/platform/14:02:b/vs680-sdhci/sdhci/sdmmc/sdmmc-mmc/user/block/part-009/block",
      "sys/platform/14:02:b/vs680-sdhci/sdhci/sdmmc/sdmmc-mmc/user/block/part-010/block",
      "sys/platform/14:02:b/vs680-sdhci/sdhci/sdmmc/sdmmc-mmc/user/block/part-011/block",
      "sys/platform/14:02:b/vs680-sdhci/sdhci/sdmmc/sdmmc-mmc/user/block/part-012/block",
      "sys/platform/14:02:d/vs680-usb-phy",
      "sys/platform/00:00:5",
      "sys/platform/00:00:9/dw-i2c/i2c/i2c-1-98",
      "sys/platform/00:00:28/dw-spi-0/spi/spi-0-0",
      "sys/platform/00:00:28/dw-spi-0/spi/spi-0-1",
      "sys/platform/14:00:e/vs680-clk",
      "sys/platform/14:02:c",
      "vs680-sdio/as370-sdhci/sdhci",
      "class/thermal/000",
      "vs680-thermal/vs680-thermal",
      "class/power/000",
      "power/vs680-power",
      "composite-pd-vcpu/power-0",
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, std::size(kDevicePaths)));
  EXPECT_EQ(zx_system_get_num_cpus(), 4);
}

TEST_F(DeviceEnumerationTest, VisaliaTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/as370",
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
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, std::size(kDevicePaths)));
  EXPECT_EQ(zx_system_get_num_cpus(), 4);
}

TEST_F(DeviceEnumerationTest, NocturneTest) {
  static const char* kDevicePaths[] = {
      "sys/pci/00:1f.3/intel-hda-000/input-stream-002",
      "sys/pci/00:1f.3/intel-hda-000/output-stream-001",
      "sys/pci/00:02.0/intel_i915/intel-gpu-core/msd-intel-gen",
      "sys/pci/00:02.0/intel_i915/display-controller",
      "sys/platform/acpi/TSR0",
      "sys/platform/acpi/TSR1",
      "sys/platform/acpi/TSR2",
      "sys/platform/acpi/TSR3",
      "sys/platform/acpi/acpi-lid/hid-device-000",
      "sys/platform/acpi/acpi-pwrbtn/hid-device-000",
      "sys/pci/00:15.0/i2c-bus-9d60/000a/i2c-hid/hid-device-000",
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, C18Test) {
  static const char* kDevicePaths[] = {
      "sys/platform/0d:00:1/mt8167-gpio",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/boot1/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/boot2/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-000/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-001/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-002/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-003/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-004/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-005/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-006/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-007/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-008/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-009/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-010/block",
      "emmc/mtk-sdmmc/sdmmc/sdmmc-mmc/user/block/part-011/block",
      "sys/platform/0d:00:e/mtk-spi-2/spi/spi-2-0",
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, std::size(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, QemuX64Q35Test) {
  static const char* kDevicePaths[] = {
      "class/sysmem/000",

      "sys/pci/00:00.0",
      "sys/pci/00:1f.2/ahci",

      "sys/platform/acpi",
      "sys/platform/acpi/acpi-pwrbtn",
      "sys/platform/acpi/i8042/i8042-keyboard",
      "sys/platform/acpi/i8042/i8042-mouse",
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, std::size(kDevicePaths)));

  if (!device_enumeration::IsAemuBoard()) {
    return;
  }
  printf("INFO: AEMU board detected. Test enumerating AEMU-specific devices.\n");

  static const char* kAemuDevicePaths[] = {
      "sys/pci/00:01.0/virtio-input",
      "sys/pci/00:02.0/virtio-input",
      "sys/pci/00:0b.0/goldfish-address-space",

      "sys/platform/acpi/goldfish",
      "sys/platform/acpi/goldfish/goldfish-pipe",

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
