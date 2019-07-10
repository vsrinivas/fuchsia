// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <fbl/algorithm.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <zircon/status.h>
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
    zx_status_t fidl_status = fuchsia_sysinfo_DeviceGetBoardName(
        channel.get(), &status, board_name, sizeof(board_name), &actual_size);
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
            printf("Checking %s\n", device_paths[i]);
            EXPECT_OK(RecursiveWaitForFile(devfs_root, device_paths[i], &fd),
                      "%s", device_paths[i]);
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
        // TODO(ZX-4069): Test SDMMC binding.
        // "sys/platform/05:00:3/aml-uart/serial/bt-transport-uart/bcm-hci",
        // "sys/platform/05:00:6/aml-sd-emmc/sdio",
        "sys/platform/05:00:2/aml-i2c/i2c/i2c-1-81/rtc",
        "sys/platform/05:00:2/aml-i2c/i2c/i2c-0-70/led2472g",
        "sys/platform/00:00:2/xhci/usb-bus",
        "sys/platform/05:02:17/aml-gpu",
        "dwmac/eth_phy/phy_null_device",
        "dwmac/Designware MAC/ethernet",
        "display/vim2-display/display-controller",
        "vim-thermal/vim-thermal",
        "gpio-light/gpio-light",
        "wifi/broadcom-wlanphy",
        "sys/platform/05:02:b/aml-mailbox",
        "class/thermal/000",
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

        // XHCI driver will not be loaded if we are in USB peripheral mode.
        // "xhci/xhci/usb-bus",

        // TODO(ZX-4087): Astro can have one of two possible touch screens
        // so we can't just test that one of them is bound. That is why the
        // following test is disabled.
        // "sys/platform/03:03:5/gt92xx HidDevice/hid-device-000",
        "sys/platform/05:00:2/aml-i2c/i2c/i2c-2-44/ti-lp8556",
        "display/astro-display/display-controller",
        "sys/platform/05:00:10/aml-canvas",
        "sys/platform/00:00:e/optee-tz",
        "sys/platform/05:03:e/amlogic_video",
        "sys/platform/00:00:f/fallback-rtc",
        "sys/platform/05:00:f/aml-raw_nand/nand/tpl/skip-block",
        "sys/platform/05:00:f/aml-raw_nand/nand/fts/skip-block",
        "sys/platform/05:00:f/aml-raw_nand/nand/factory/skip-block",
        "sys/platform/05:00:f/aml-raw_nand/nand/zircon-b/skip-block",
        "sys/platform/05:00:f/aml-raw_nand/nand/zircon-a/skip-block",
        "sys/platform/05:00:f/aml-raw_nand/nand/zircon-r/skip-block",
        "sys/platform/05:00:f/aml-raw_nand/nand/sys-config/skip-block",
        "sys/platform/05:00:f/aml-raw_nand/nand/migration/skip-block",
        "sys/platform/05:00:7/aml-sd-emmc/sdmmc",
        "sys/platform/05:00:7/aml-sd-emmc/sdmmc/sdmmc-sdio",
        "sys/platform/05:00:7/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-1/component",
        "sys/platform/05:00:7/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-2/component",
        "wifi",
        "tcs3400-light/tcs-3400/hid-device-000",
        "sys/platform/05:03:11/clocks",
        "sys/platform/05:03:a/thermal",
        "AstroAudio/astro-audio-out",
        "sys/platform/05:03:13/astro-audio-in",
        //"sys/platform/05:05:3/aml-uart/serial/bt-transport-uart/bcm-hci",
    };

    ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, fbl::count_of(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, CleoTest) {
    static const char* kDevicePaths[] = {
        "sys/platform/mt8167s_ref",
        "sys/platform/0d:00:1/mt8167-gpio",
        "sys/platform/0d:00:8/mtk-clk",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-000/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-001/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-002/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-003/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-004/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-005/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-006/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-007/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-008/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-009/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-010/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-011/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-012/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-013/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-014/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-015/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-016/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-017/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-018/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-019/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-020/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-021/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-022/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-023/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-024/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-025/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-026/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-027/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-028/block",
        "sys/platform/0d:00:4/mtk-sdmmc",
        "mt8167-buttons/hid-buttons",
        "sys/platform/0d:00:14/mt-usb/usb-peripheral/function-000/cdc-eth-function/ethernet",
        "touch/focaltouch HidDevice/hid-device-000",
        "sys/platform/0d:00:9/mtk-thermal",
        "mt8167-i2c",
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
        "sys/platform/05:03:a/thermal",
        "sys/platform/00:00:1e/dw-dsi",
        "display/astro-display/display-controller",
        "sys/platform/05:00:18/aml-usb-phy-v2",

        // XHCI driver will not be loaded if we are in USB peripheral mode.
        // "xhci/xhci/usb-bus",

        "sys/platform/05:00:8/aml-sd-emmc/sdmmc/sdmmc-mmc/block/part-000/block",
        "sys/platform/05:00:8/aml-sd-emmc/sdmmc/sdmmc-mmc/block/part-002/block",
        "sys/platform/05:00:8/aml-sd-emmc/sdmmc/sdmmc-mmc/block/part-003/block",
        "sys/platform/05:00:8/aml-sd-emmc/sdmmc/sdmmc-mmc/block/part-004/block",
        "sys/platform/05:00:8/aml-sd-emmc/sdmmc/sdmmc-mmc/block/part-005/block",
        "sys/platform/05:00:8/aml-sd-emmc/sdmmc/sdmmc-mmc/block/part-006/block",
        "sys/platform/05:00:8/aml-sd-emmc/sdmmc/sdmmc-mmc/block/part-007/block",
        "sys/platform/05:00:8/aml-sd-emmc/sdmmc/sdmmc-mmc/block/part-008/block/fvm/blobfs-p-1/block",
        "sys/platform/05:00:8/aml-sd-emmc/sdmmc/sdmmc-mmc/block/part-008/block/fvm/minfs-p-2/block",
        "sys/platform/05:00:8/aml-sd-emmc/sdmmc/sdmmc-mmc/block/part-009/block",
        "sys/platform/05:00:8/aml-sd-emmc/sdmmc/sdmmc-mmc/block/part-010/block",
        "sys/platform/05:00:6/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-1",
        "sys/platform/05:00:6/aml-sd-emmc/sdmmc/sdmmc-sdio/sdmmc-sdio-2",
        "wifi",
        "sys/platform/05:04:15/aml-mipi",
        "sys/platform/12:02:2/gdc",
        "imx227-sensor",
        "isp",
        "sys/platform/05:04:e/amlogic_video",
        "sys/platform/05:04:17/aml-gpu",
        "sys/platform/05:04:16/sherlock-audio-in",
        "SherlockAudio",
        "ft5726-touch",
        "sys/platform/00:00:e/optee-tz",
        "sys/platform/00:00:f/fallback-rtc",
        "spi/aml-spi-0/spi/spi-0-0",
        "sherlock-buttons/hid-buttons",
    };

    ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, fbl::count_of(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, Mt8167sRefTest) {
    static const char* kDevicePaths[] = {
        "sys/platform/mt8167s_ref",
        "sys/platform/0d:00:1/mt8167-gpio",
        "sys/platform/0d:00:6/mt8167-i2c",
        "sys/platform/0d:00:8/mtk-clk",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-000/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-001/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-002/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-003/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-004/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-005/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-006/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-007/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-008/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-009/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-010/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-011/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-012/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-013/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-014/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-015/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-016/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-017/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-018/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-019/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-020/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-021/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-022/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-023/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-024/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-025/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-026/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-027/block",
        "sys/platform/0d:00:2/mtk-sdmmc/sdmmc/sdmmc-mmc/block/part-028/block",
        "sys/platform/0d:00:4/mtk-sdmmc",
        "mt8167-buttons/hid-buttons",
        "sys/platform/0d:00:14/mt-usb/usb-peripheral/function-000/cdc-eth-function/ethernet",
        "sys/platform/0d:00:9/mtk-thermal",
    };

    ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, fbl::count_of(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, Msm8x53SomTest) {
    static const char* kDevicePaths[] = {
        "sys/platform/msm8x53",
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
        "dwc2-usb",
        "audio-max98373",
        "as370-audio-out",
    };

    ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, fbl::count_of(kDevicePaths)));
}

TEST_F(DeviceEnumerationTest, VisaliaTest) {
    static const char* kDevicePaths[] = {
        "sys/platform/as370",
        "sys/platform/14:01:1",
        "sys/platform/14:01:1/as370-gpio",
        "sys/platform/00:00:9",
        "sys/platform/00:00:9/dw-i2c",
        "sys/platform/14:01:2/as370-usb-phy",
        "dwc2-usb",
    };

    ASSERT_NO_FATAL_FAILURES(TestRunner(kDevicePaths, fbl::count_of(kDevicePaths)));
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

} // namespace

int main(int argc, char** argv) {
    fbl::Vector<fbl::String> errors;
    auto options = zxtest::Runner::Options::FromArgs(argc, argv, &errors);
    zxtest::LogSink* log_sink =
        zxtest::Runner::GetInstance()->mutable_reporter()->mutable_log_sink();


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
