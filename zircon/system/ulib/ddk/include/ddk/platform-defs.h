// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>

__BEGIN_CDECLS

// Vendor, Product and Device IDs for generic platform drivers
#define PDEV_VID_GENERIC            0
#define PDEV_PID_GENERIC            0
#define PDEV_DID_USB_DWC3           1   // DWC3 USB Controller
#define PDEV_DID_USB_XHCI           2   // XHCI USB Controller
#define PDEV_DID_KPCI               3   // Syscall based PCI driver
#define PDEV_DID_ARM_MALI           4   // ARM MALI GPU
#define PDEV_DID_USB_DWC2           5   // DWC2 USB Controller
#define PDEV_DID_RTC_PL031          6   // ARM Primecell PL031 RTC
#define PDEV_DID_DSI                7   // DSI
#define PDEV_DID_GPIO_TEST          8   // Simple GPIO test driver
#define PDEV_DID_DW_I2C             9   // Designware I2C
#define PDEV_DID_DW_PCIE            10  // Designware PCIe
#define PDEV_DID_LED2472G           11  // RPi Sense Hat LED2472G
#define PDEV_DID_GPU_VSL_GC         12  // Verisilicon GC series GPU
#define PDEV_DID_ARM_MALI_INIT      13  // ARM MALI GPU (Zircon)
#define PDEV_DID_OPTEE              14  // OP-TEE OS Driver
#define PDEV_DID_RTC_FALLBACK       15  // Synthetic RTC device
#define PDEV_DID_ETH_MAC            16  // Ethernet MAC
#define PDEV_DID_ETH_PHY            17  // Ethernet PHY
#define PDEV_DID_CAMERA_SENSOR      18  // Camera Sensor
#define PDEV_DID_HID_BUTTONS        19  // HID Buttons
#define PDEV_DID_MUSB_PERIPHERAL    20  // MUSB in peripheral role
#define PDEV_DID_MUSB_HOST          21  // MUSB in host role
#define PDEV_DID_DUMMY_DISPLAY      22  // Dummy display
#define PDEV_DID_FOCALTOUCH         23  // FocalTech touch device
#define PDEV_DID_LITE_ON_ALS        24  // Lite-On ambient light sensor
#define PDEV_DID_BOSCH_BMA253       25  // Bosch BMA253 acceleration sensor
#define PDEV_DID_SG_MICRO_SGM37603A 26  // SG Micro SGM37603A backlight driver
#define PDEV_DID_SYSMEM             27  // Sysmem driver
#define PDEV_DID_GPIO_LIGHT         28  // Driver for controlling lights via GPIOs
#define PDEV_DID_CPU_TRACE          29  // CPU tracing driver
#define PDEV_DID_DW_DSI             30  // Designware DSI

// QEMU emulator
#define PDEV_VID_QEMU               1
#define PDEV_PID_QEMU               1

// 96Boards
#define PDEV_VID_96BOARDS           2
#define PDEV_PID_HIKEY960           1

#define PDEV_DID_HIKEY960_I2C_TEST  1
#define PDEV_DID_HI3660_CLK         2
#define PDEV_DID_HIKEY960_USB       3
#define PDEV_DID_HI_DISPLAY         4
#define PDEV_DID_HISILICON_UFS      5

// Google
#define PDEV_VID_GOOGLE             3
#define PDEV_PID_GAUSS              1
#define PDEV_PID_MACHINA            2
#define PDEV_PID_ASTRO              3
// 4 unused
#define PDEV_PID_SHERLOCK           5
#define PDEV_PID_CLEO               6
#define PDEV_PID_EAGLE              7

#define PDEV_DID_GAUSS_AUDIO_IN     1
#define PDEV_DID_GAUSS_AUDIO_OUT    2
#define PDEV_DID_GAUSS_I2C_TEST     3
#define PDEV_DID_GAUSS_LED          4
#define PDEV_DID_ASTRO_GOODIXTOUCH  5

// Khadas
#define PDEV_VID_KHADAS             4
#define PDEV_PID_VIM2               2
#define PDEV_PID_VIM2_MACHINA       1002

#define PDEV_DID_VIM_DISPLAY        1

// Amlogic
#define PDEV_VID_AMLOGIC            5
#define PDEV_PID_AMLOGIC_A113       1
#define PDEV_PID_AMLOGIC_S905X      2
#define PDEV_PID_AMLOGIC_S912       3
#define PDEV_PID_AMLOGIC_S905       4
#define PDEV_PID_AMLOGIC_S905D2     5
#define PDEV_PID_AMLOGIC_T931       6

#define PDEV_DID_AMLOGIC_GPIO       1
#define PDEV_DID_AMLOGIC_I2C        2
#define PDEV_DID_AMLOGIC_UART       3
#define PDEV_DID_AMLOGIC_AXG_CLK    4
#define PDEV_DID_AMLOGIC_GXL_CLK    5
#define PDEV_DID_AMLOGIC_SD_EMMC_A  6
#define PDEV_DID_AMLOGIC_SD_EMMC_B  7
#define PDEV_DID_AMLOGIC_SD_EMMC_C  8
#define PDEV_DID_AMLOGIC_ETH        9
#define PDEV_DID_AMLOGIC_THERMAL    10
#define PDEV_DID_AMLOGIC_MAILBOX    11
#define PDEV_DID_AMLOGIC_SCPI       12
#define PDEV_DID_AMLOGIC_DISPLAY    13
#define PDEV_DID_AMLOGIC_VIDEO      14
#define PDEV_DID_AMLOGIC_RAW_NAND   15
#define PDEV_DID_AMLOGIC_CANVAS     16
#define PDEV_DID_AMLOGIC_G12A_CLK   17
#define PDEV_DID_AMLOGIC_TDM        18
#define PDEV_DID_ASTRO_PDM          19
#define PDEV_DID_AMLOGIC_G12B_CLK   20
#define PDEV_DID_AMLOGIC_MIPI_CSI   21
#define PDEV_DID_SHERLOCK_PDM       22


// Broadcom
#define PDEV_VID_BROADCOM           6
#define PDEV_PID_BCM4356            1
#define PDEV_PID_BCM43458           2

//Hardkernel
#define PDEV_VID_HARDKERNEL         7
#define PDEV_PID_ODROID_C2          1

// Intel
#define PDEV_VID_INTEL              8
#define PDEV_PID_X86                1

// NXP
#define PDEV_VID_NXP                9
#define PDEV_PID_IMX8MMEVK          1

#define PDEV_DID_IMX_GPIO           1
#define PDEV_DID_IMX_DISPLAY        2
#define PDEV_DID_IMX_SDHCI          3
#define PDEV_DID_IMX_I2C            4
#define PDEV_DID_PCF8563_RTC        5

// AMS
#define PDEV_VID_AMS                10
#define PDEV_PID_AMS_TCS3400        1
#define PDEV_DID_AMS_LIGHT          1

// REALTEK
#define PDEV_VID_REALTEK            11
#define PDEV_PID_RTL8211F           1

// Designware
#define PDEV_VID_DESIGNWARE         12

// Mediatek
#define PDEV_VID_MEDIATEK           13
#define PDEV_PID_MEDIATEK_8167S_REF 1
#define PDEV_DID_MEDIATEK_GPIO      1
#define PDEV_DID_MEDIATEK_MSDC0     2
#define PDEV_DID_MEDIATEK_MSDC1     3
#define PDEV_DID_MEDIATEK_MSDC2     4
#define PDEV_DID_MEDIATEK_DISPLAY   5
#define PDEV_DID_MEDIATEK_I2C       6
#define PDEV_DID_MEDIATEK_GPU       7
#define PDEV_DID_MEDIATEK_CLK       8
#define PDEV_DID_MEDIATEK_THERMAL   9
#define PDEV_DID_MEDIATEK_AUDIO_OUT 10
#define PDEV_DID_MEDIATEK_AUDIO_IN  11
#define PDEV_DID_MEDIATEK_DSI       12

// Sony
#define PDEV_VID_SONY               14
#define PDEV_PID_SONY_IMX227        1

// Hisilicon
#define PDEV_VID_HISILICON          15
#define PDEV_PID_CORNEL             1

// Texas Instruments
#define PDEV_VID_TI                 16
#define PDEV_PID_TI_LP8556          1
#define PDEV_DID_TI_BACKLIGHT       1

// Test
#define PDEV_VID_TEST               17
#define PDEV_PID_PBUS_TEST          1
#define PDEV_PID_INTEGRATION_TEST   2

#define PDEV_DID_TEST_PARENT        1
#define PDEV_DID_TEST_CHILD_1       2
#define PDEV_DID_TEST_CHILD_2       3
#define PDEV_DID_TEST_CHILD_3       4
#define PDEV_DID_TEST_GPIO          5
#define PDEV_DID_TEST_COMPOSITE     6

// ARM
#define PDEV_VID_ARM                18
#define PDEV_PID_ISP                1
#define PDEV_DID_ARM_MALI_IV009     1

// Qualcomm
#define PDEV_VID_QUALCOMM           19
#define PDEV_PID_QUALCOMM_MSM8X53    1
#define PDEV_DID_QUALCOMM_GPIO       1

__END_CDECLS
