// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <zircon/status.h>

#include <fbl/algorithm.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <zxtest/base/log-sink.h>
#include <zxtest/zxtest.h>

namespace {

using devmgr_integration_test::RecursiveWaitForFile;

fbl::String GetTestFilter() {
  constexpr char kSysInfoPath[] = "/dev/misc/sysinfo";
  fbl::unique_fd sysinfo(open(kSysInfoPath, O_RDWR));
  if (!sysinfo) {
    return "Unknown";
  }
  zx::channel channel;
  if (fdio_get_service_handle(sysinfo.release(), channel.reset_and_get_address()) != ZX_OK) {
    return "Unknown";
  }

  char board_name[fuchsia_sysinfo_SYSINFO_BOARD_NAME_LEN + 1];
  zx_status_t status;
  size_t actual_size;
  zx_status_t fidl_status = fuchsia_sysinfo_DeviceGetBoardName(channel.get(), &status, board_name,
                                                               sizeof(board_name), &actual_size);
  if (fidl_status != ZX_OK || status != ZX_OK) {
    return "Unknown";
  }
  board_name[actual_size] = '\0';

  printf("Found board %s\n", board_name);

  if (!strcmp(board_name, "qemu")) {
    return "*Qemu*";
  } else if (!strcmp(board_name, "vim2")) {
    return "*Vim2*";
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
  } else if (!strcmp(board_name, "hikey960")) {
    return "*Hikey960*";
  } else if (!strcmp(board_name, "Nocturne")) {
    return "*Nocturne*";
  } else if (!strcmp(board_name, "c18")) {
    return "*C18*";
  } else if (!strcmp(board_name, "nelson")) {
    return "*Nelson*";
  }

  return "Unknown";
}

class DeviceEnumerationTest : public zxtest::Test {
 protected:
  void TestRunner(const char** device_paths, size_t paths_num) {
    fbl::unique_fd devfs_root(open("/dev", O_RDWR));
    ASSERT_TRUE(devfs_root);

    fbl::unique_fd fd;
    for (size_t i = 0; i < paths_num; ++i) {
      // stderr helps diagnosibility, since stdout doesn't show up in test logs
      fprintf(stderr, "Checking %s\n", device_paths[i]);
      EXPECT_OK(RecursiveWaitForFile(devfs_root, device_paths[i], &fd), "%s", device_paths[i]);
    }
  }
};

TEST_F(DeviceEnumerationTest, QemuTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/qemu-bus",
      "sys/platform/00:00:6/rtc",
      "sys/pci/00:00.0",
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, fbl::count_of(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, Vim2Test) {
  static const char* kDevicePaths[] = {
      "sys/platform/vim",
      "sys/platform/00:00:1b/sysmem",
      "sys/platform/05:02:1/aml-gxl-gpio",
      "sys/platform/05:00:2/aml-i2c",
      "sys/platform/05:02:4/clocks",
      "sys/platform/05:00:10/aml-canvas",
      "aml_emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/boot1/block",
      "aml_emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/boot2/block",
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
      "aml-sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-1/component",
      "aml-sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-2/component",
      "sys/platform/05:02:b/aml-mailbox",
      "class/thermal/000",
      "aml-video/amlogic_video",
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, fbl::count_of(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, AstroTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/astro",
      "sys/platform/05:03:1/aml-axg-gpio",
      "astro-buttons/hid-buttons",
      "sys/platform/05:00:2/aml-i2c",
      "sys/platform/05:03:17/aml-gpu",
      "sys/platform/05:00:18/aml-usb-phy-v2",
      "sys/platform/05:03:1e/cpu",

      // XHCI driver will not be loaded if we are in USB peripheral mode.
      // "xhci/xhci/usb-bus",

      // TODO(ZX-4087): Astro can have one of two possible touch screens
      // so we can't just test that one of them is bound. That is why the
      // following test is disabled.
      // "sys/platform/03:03:5/gt92xx HidDevice/hid-device-000",
      "backlight/ti-lp8556",
      "display/astro-display/display-controller",
      "sys/platform/05:00:10/aml-canvas",
      "tee/optee-tz",
      "aml-video/amlogic_video",
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
      "aml-sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-1/component",
      "aml-sdio/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-2/component",
      "wifi/brcmfmac-wlanphy",
      "tcs3400-light/tcs-3400/hid-device-000",
      "sys/platform/05:03:11/clocks",
      "aml-thermal/thermal",
      "AstroAudio/astro-audio-out",
      "sys/platform/05:03:13/astro-audio-in",
      "aml-secure-mem/aml-securemem",
      //"sys/platform/05:05:3/aml-uart/serial/bt-transport-uart/bcm-hci",
      "pwm-init",
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, fbl::count_of(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, NelsonTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/nelson",
      "sys/platform/05:03:1/aml-axg-gpio",
      "nelson-buttons/hid-buttons",
      "sys/platform/05:00:2/aml-i2c",
      "sys/platform/05:03:17/aml-gpu",
      "sys/platform/05:00:18/aml-usb-phy-v2",
      "sys/platform/05:03:1e/cpu",

      // XHCI driver will not be loaded if we are in USB peripheral mode.
      // "xhci/xhci/usb-bus",

      // TODO(ZX-4087): Nelson can have one of two possible touch screens
      // so we can't just test that one of them is bound. That is why the
      // following test is disabled.
      // "sys/platform/03:03:5/gt92xx HidDevice/hid-device-000",
      "backlight/ti-lp8556",
      "sys/platform/05:00:10/aml-canvas",
      "tee/optee-tz",
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
      "sys/platform/05:03:11/clocks",
      "aml-thermal/thermal",
      "NelsonAudio/nelson-audio-out",
      "sys/platform/05:03:13/nelson-audio-in",
      "aml-secure-mem/aml-securemem",
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, fbl::count_of(kDevicePaths)));
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

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, fbl::count_of(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, SherlockTest) {
  static const char* kDevicePaths[] = {
      "sys/platform/sherlock",
      "sys/platform/05:04:1/aml-axg-gpio",
      "sys/platform/05:00:14/clocks",
      "sys/platform/05:00:2/aml-i2c",
      "sys/platform/05:00:10/aml-canvas",
      // Disabling this driver temporarily
      // TODO(35875): Re-enable when this is fixed.
      //"aml-thermal/thermal",
      "sys/platform/00:00:1e/dw-dsi",
      "display/astro-display/display-controller",
      "sys/platform/05:00:18/aml-usb-phy-v2",

      // XHCI driver will not be loaded if we are in USB peripheral mode.
      // "xhci/xhci/usb-bus",

      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/boot1/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/boot2/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-000/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-002/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-000/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-002/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-003/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-004/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-005/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-006/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-007/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-008/block/fvm/blobfs-p-1/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-008/block/fvm/minfs-p-2/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-009/block",
      "sherlock-emmc/aml-sd-emmc/sdmmc/sdmmc-mmc/user/block/part-010/block",
      "sherlock-sd-emmc/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-1",
      "sherlock-sd-emmc/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-2",
      "wifi/brcmfmac-wlanphy",
      "sys/platform/05:04:15/aml-mipi",
      "sys/platform/05:04:1c",  // nna
      "sys/platform/05:04:1d",  // pwm
      "gdc",
      "ge2d",
      "gpio-light",
      "imx227-sensor",
      "isp",
      "camera-controller",
      "aml-video/amlogic_video",
      "sys/platform/05:04:17/aml-gpu",
      "sys/platform/05:04:16/sherlock-audio-in",
      "SherlockAudio",
      "ft5726-touch",
      "tee/optee-tz",
      "sys/platform/00:00:f/fallback-rtc",
      "spi/aml-spi-0/spi/spi-0-0",
      "sherlock-buttons/hid-buttons",
      "nrf52840-radio",
      "backlight/ti-lp8556",
      "SherlockLightSensor/tcs-3400/hid-device-000",
      "aml-secure-mem/aml-securemem",
      "pwm-init",
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, fbl::count_of(kDevicePaths)));
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

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, fbl::count_of(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, Msm8x53SomTest) {
  static const char* kDevicePaths[] = {"sys/platform/msm8x53",
                                       "sys/platform/13:01:1",
                                       "sys/platform/13:00:3/msm8x53-sdhci",
                                       "sys/platform/13:00:2/qcom-pil",
                                       "sys/platform/13:01:4/msm-clk",
                                       "sys/platform/13:01:5/msm8x53-power"};

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, fbl::count_of(kDevicePaths)));
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
      "sys/platform/00:00:22/cadence-hpnfc/nand/cache/ftl/block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/tzk_normal/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/tzk_normalB/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/bl_normal/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/bl_normalB/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/boot/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/rootfs/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/recovery/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/fts/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/factory_store/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/key_1st/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/key_2nd/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/fastboot_1st/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/fastboot_2nd/skip-block",
      "power/as370-power",
      "power/as370-power/power-0",
      "class/thermal/000",
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, fbl::count_of(kDevicePaths)));
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
      "dwc2-usb",
      "sys/platform/00:00:22/cadence-hpnfc/nand/cache/ftl/block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/tzk_normal/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/tzk_normalB/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/bl_normal/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/bl_normalB/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/boot/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/rootfs/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/recovery/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/fts/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/factory_store/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/key_1st/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/key_2nd/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/fastboot_1st/skip-block",
      "sys/platform/00:00:22/cadence-hpnfc/nand/fastboot_2nd/skip-block",
      "power/as370-power",
      "power/as370-power/power-0",
      "class/thermal/000",
      "lp5018-light",
      "lp5018-light/lp50xx-light",
      "as370-touch",
      "as370-touch/cy8cmbr3108",
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, fbl::count_of(kDevicePaths)));
  EXPECT_EQ(zx_system_get_num_cpus(), 4);
}

TEST_F(DeviceEnumerationTest, Hikey960Test) {
  static const char* kDevicePaths[] = {
      "sys/platform/hikey960",
      "sys/platform/02:00:6/hi3660-gpio",
      "sys/platform/00:00:9/dw-i2c",
      "sys/platform/02:00:2/hi3660-clk",
      "hikey-usb/dwc3",
      "dwc3/dwc3/usb-peripheral",
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, fbl::count_of(kDevicePaths)));
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

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, fbl::count_of(kDevicePaths)));
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
  };

  ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, fbl::count_of(kDevicePaths)));
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
