// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_CPP_ZIRCON_BOOT_OPS_H_
#define SRC_FIRMWARE_GIGABOOT_CPP_ZIRCON_BOOT_OPS_H_

#include <lib/zircon_boot/zircon_boot.h>

namespace gigaboot {
ZirconBootOps GetZirconBootOps();
}

#endif  // SRC_FIRMWARE_GIGABOOT_CPP_ZIRCON_BOOT_OPS_H_
