// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>

__BEGIN_CDECLS;

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

// QEMU emulator
#define PDEV_VID_QEMU               1
#define PDEV_PID_QEMU               1

// 96Boards
#define PDEV_VID_96BOARDS           2
#define PDEV_PID_HIKEY960           1

#define PDEV_DID_HIKEY960_I2C_TEST  1

// Google
#define PDEV_VID_GOOGLE             3
#define PDEV_PID_GAUSS              1
#define PDEV_PID_MACHINA            2
#define PDEV_PID_ASTRO              3
#define PDEV_PID_MADRONE            4

#define PDEV_DID_GAUSS_AUDIO_IN     1
#define PDEV_DID_GAUSS_AUDIO_OUT    2
#define PDEV_DID_GAUSS_I2C_TEST     3
#define PDEV_DID_GAUSS_LED          4
#define PDEV_DID_ASTRO_FOCALTOUCH   5
#define PDEV_DID_ASTRO_GOODIXTOUCH  6


// Khadas
#define PDEV_VID_KHADAS             4
#define PDEV_PID_VIM2               2

#define PDEV_DID_VIM_DISPLAY        1

// Amlogic
#define PDEV_VID_AMLOGIC            5
#define PDEV_PID_AMLOGIC_A113       1
#define PDEV_PID_AMLOGIC_S905X      2
#define PDEV_PID_AMLOGIC_S912       3
#define PDEV_PID_AMLOGIC_S905       4
#define PDEV_PID_AMLOGIC_S905D2     5

#define PDEV_DID_AMLOGIC_GPIO       1
#define PDEV_DID_AMLOGIC_I2C        2
#define PDEV_DID_AMLOGIC_UART       3
#define PDEV_DID_AMLOGIC_AXG_CLK    4
#define PDEV_DID_AMLOGIC_GXL_CLK    5
#define PDEV_DID_AMLOGIC_SD_EMMC    6
#define PDEV_DID_AMLOGIC_ETH        7
#define PDEV_DID_AMLOGIC_THERMAL    8
#define PDEV_DID_AMLOGIC_MAILBOX    9
#define PDEV_DID_AMLOGIC_SCPI       10
#define PDEV_DID_AMLOGIC_DISPLAY    11
#define PDEV_DID_AMLOGIC_VIDEO      12
#define PDEV_DID_AMLOGIC_RAW_NAND   13
#define PDEV_DID_AMLOGIC_CANVAS     14

// Broadcom
#define PDEV_VID_BROADCOM           6
#define PDEV_PID_BCM4356            1
#define PDEV_PID_BCM43458           2

//Hardkernel
#define PDEV_VID_HARDKERNEL         7
#define PDEV_PID_ODROID_C2          1

// Intel
#define PDEV_VID_INTEL              8
#define PDEV_DID_INTEL_CPU_TRACE    1 // Intel CPU tracing driver

// NXP
#define PDEV_VID_NXP                9
#define PDEV_PID_IMX8MEVK           1
#define PDEV_PID_PCF8563            2

#define PDEV_DID_IMX_GPIO           1
#define PDEV_DID_IMX_DISPLAY        2
#define PDEV_DID_IMX_SDHCI          3

#define PDEV_DID_PCF8563_RTC        1

__END_CDECLS;
