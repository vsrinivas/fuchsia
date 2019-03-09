// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_BOOT_SYSCONFIG_H_
#define SYSROOT_ZIRCON_BOOT_SYSCONFIG_H_

// Zircon sysconfig partition format
//
// The sysconfig partition consists of four kvstore sections, each 32K in size.
// The sections are:
//
// version-a:       System configuration used when booting from Zircon-A.
//
// version-b:       System configuration used when booting from Zircon-B.
//
// boot-default:    Default bootloader configuration.
//
// boot-oneshot:    Bootloader configuration for one-time use.
//                  If present, this overrides boot-default, and the bootloader
//                  deletes this section after use.

#define ZX_SYSCONFIG_KVSTORE_SIZE 32768
#define ZX_SYSCONFIG_VERSION_A_OFFSET (0 * ZX_SYSCONFIG_KVSTORE_SIZE)
#define ZX_SYSCONFIG_VERSION_B_OFFSET (1 * ZX_SYSCONFIG_KVSTORE_SIZE)
#define ZX_SYSCONFIG_BOOT_DEFAULT_OFFSET (2 * ZX_SYSCONFIG_KVSTORE_SIZE)
#define ZX_SYSCONFIG_BOOT_ONESHOT_OFFSET (3 * ZX_SYSCONFIG_KVSTORE_SIZE)

#endif  // SYSROOT_ZIRCON_BOOT_SYSCONFIG_H_
