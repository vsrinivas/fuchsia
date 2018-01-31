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

// QEMU emulator
#define PDEV_VID_QEMU               1
#define PDEV_PID_QEMU               1

// 96Boards
#define PDEV_VID_96BOARDS           2
#define PDEV_PID_HIKEY960           1

#define PDEV_DID_HIKEY960_GPIO_TEST 1
#define PDEV_DID_HIKEY960_I2C_TEST  2

// Google
#define PDEV_VID_GOOGLE             3
#define PDEV_PID_GAUSS              1

#define PDEV_DID_GAUSS_AUDIO_IN     1
#define PDEV_DID_GAUSS_AUDIO_OUT    2
#define PDEV_DID_GAUSS_I2C_TEST     3
#define PDEV_DID_GAUSS_LED          4

// Khadas
#define PDEV_VID_KHADAS             4
#define PDEV_PID_VIM                1
#define PDEV_PID_VIM2               2

#define PDEV_DID_VIM_DISPLAY        1
#define PDEV_DID_VIM_GPIO_TEST      2

// Amlogic
#define PDEV_VID_AMLOGIC            5
#define PDEV_PID_AMLOGIC_A113       1
#define PDEV_PID_AMLOGIC_S905X      2
#define PDEV_PID_AMLOGIC_S912       3

#define PDEV_DID_AMLOGIC_GPIO       1
#define PDEV_DID_AMLOGIC_I2C        2

__END_CDECLS;
