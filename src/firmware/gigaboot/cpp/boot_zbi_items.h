// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_CPP_BOOT_ZBI_ITEMS_H_
#define SRC_FIRMWARE_GIGABOOT_CPP_BOOT_ZBI_ITEMS_H_

#include <lib/zbi/zbi.h>

namespace gigaboot {
bool AddGigabootZbiItems(zbi_header_t *image, size_t capacity, AbrSlotIndex slot);
}  // namespace gigaboot

#endif  // SRC_FIRMWARE_GIGABOOT_CPP_BOOT_ZBI_ITEMS_H_
