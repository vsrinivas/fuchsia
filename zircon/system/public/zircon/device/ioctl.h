// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_DEVICE_IOCTL_H_
#define SYSROOT_ZIRCON_DEVICE_IOCTL_H_

// clang-format off

// DEFAULT ioctls accept and received byte[] data
// the particular ioctl may define more specific structures
#define IOCTL_KIND_DEFAULT          0x0

// core device/vfs ioctl families
#define IOCTL_FAMILY_NETCONFIG      0x26

// IOCTL constructor
// --K-FFNN
#define IOCTL(kind, family, number) \
    ((((kind) & 0xF) << 20) | (((family) & 0xFF) << 8) | ((number) & 0xFF))

// IOCTL accessors
#define IOCTL_KIND(n) (((n) >> 20) & 0xF)
#define IOCTL_FAMILY(n) (((n) >> 8) & 0xFF)
#define IOCTL_NUMBER(n) ((n) & 0xFF)

#endif  // SYSROOT_ZIRCON_DEVICE_IOCTL_H_
