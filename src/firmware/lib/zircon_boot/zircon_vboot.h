// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_LIB_ZIRCON_BOOT_ZIRCON_VBOOT_H_
#define SRC_FIRMWARE_LIB_ZIRCON_BOOT_ZIRCON_VBOOT_H_

#include <lib/zircon_boot/zircon_boot.h>

// ZirconVBootSlotVerify() - Verifies a preloaded kernel if the device is locked.
// If unlocked, this returns true. The function is for internal use.
bool ZirconVBootSlotVerify(ZirconBootOps* zb_ops, zbi_header_t* image, size_t capacity,
                           const char* ab_suffix, bool has_successfully_booted);

#endif  // SRC_FIRMWARE_LIB_ZIRCON_BOOT_ZIRCON_VBOOT_H_
