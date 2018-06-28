// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

// DEFAULT ioctls accept and received byte[] data
// the particular ioctl may define more specific structures
#define IOCTL_KIND_DEFAULT          0x0

// GET_HANDLE ioctls accept plain data and return
// a single handle, optionally followed by plain data
#define IOCTL_KIND_GET_HANDLE       0x1

// GET_TWO_HANDLES ioctls accept plain data and return
// two handles, optionally followed by plain data
#define IOCTL_KIND_GET_TWO_HANDLES  0x2

// GET_THREE_HANDLES ioctls accept plain data and return
// three handles, optionally followed by plain data
#define IOCTL_KIND_GET_THREE_HANDLES 0x4

// SET_HANDLE ioctls accept a handle, and optionally
// plain data afterwards.
#define IOCTL_KIND_SET_HANDLE       0x3

// SET_TWO_HANDLES ioctls accepts two handles, and
// optionally plain data afterwards.
#define IOCTL_KIND_SET_TWO_HANDLES  0x5

// core device/vfs ioctl families
#define IOCTL_FAMILY_RESERVED       0x00
#define IOCTL_FAMILY_DEVICE         0x01
#define IOCTL_FAMILY_VFS            0x02
#define IOCTL_FAMILY_DMCTL          0x03
#define IOCTL_FAMILY_TEST           0x04

// device protocol families
#define IOCTL_FAMILY_CONSOLE        0x10
#define IOCTL_FAMILY_INPUT          0x11
#define IOCTL_FAMILY_DISPLAY        0x12
#define IOCTL_FAMILY_BLOCK          0x13
#define IOCTL_FAMILY_I2C            0x14
#define IOCTL_FAMILY_TPM            0x15
#define IOCTL_FAMILY_USB            0x16
#define IOCTL_FAMILY_HID            0x17
// 0x18 unused
#define IOCTL_FAMILY_AUDIO          0x19
#define IOCTL_FAMILY_MIDI           0x1A
#define IOCTL_FAMILY_KTRACE         0x1B
#define IOCTL_FAMILY_BT_HCI         0x1C
#define IOCTL_FAMILY_SYSINFO        0x1D
#define IOCTL_FAMILY_GPU            0x1E
#define IOCTL_FAMILY_RTC            0x1F  // ioctls for RTC
#define IOCTL_FAMILY_ETH            0x20
#define IOCTL_FAMILY_IPT            0x21  // ioctls for Intel PT
#define IOCTL_FAMILY_RAMDISK        0x22
#define IOCTL_FAMILY_SDMMC          0x23
#define IOCTL_FAMILY_WLAN           0x24
#define IOCTL_FAMILY_PTY            0x25
#define IOCTL_FAMILY_NETCONFIG      0x26
#define IOCTL_FAMILY_ETHERTAP       0x27
#define IOCTL_FAMILY_USB_DEVICE     0x28
#define IOCTL_FAMILY_USB_VIRT_BUS   0x29
#define IOCTL_FAMILY_CPUPERF        0x2A
#define IOCTL_FAMILY_POWER          0x30
#define IOCTL_FAMILY_THERMAL        0x31
#define IOCTL_FAMILY_CAMERA         0x32
#define IOCTL_FAMILY_BT_HOST        0x33
#define IOCTL_FAMILY_WLANPHY        0x34
#define IOCTL_FAMILY_SERIAL         0x35
#define IOCTL_FAMILY_WLANTAP        0x36
#define IOCTL_FAMILY_DISPLAY_CONTROLLER 0x37
#define IOCTL_FAMILY_DEBUG          0x38
#define IOCTL_FAMILY_AUDIO_CODEC    0x39
#define IOCTL_FAMILY_BACKLIGHT      0x3A
#define IOCTL_FAMILY_NAND_TEST      0x3B
#define IOCTL_FAMILY_TEE            0x3C
#define IOCTL_FAMILY_SKIP_BLOCK     0x3D

// IOCTL constructor
// --K-FFNN
#define IOCTL(kind, family, number) \
    ((((kind) & 0xF) << 20) | (((family) & 0xFF) << 8) | ((number) & 0xFF))

// IOCTL accessors
#define IOCTL_KIND(n) (((n) >> 20) & 0xF)
#define IOCTL_FAMILY(n) (((n) >> 8) & 0xFF)
#define IOCTL_NUMBER(n) ((n) & 0xFF)
