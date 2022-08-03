// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <zircon/assert.h>

#include "backends.h"
#include "gigaboot/src/bootbyte.h"

namespace gigaboot {

bool SetRebootMode(RebootMode mode) {
  switch (mode) {
    case RebootMode::kNormal:
      bootbyte_set_normal();
      return true;
    case RebootMode::kBootloader:
      bootbyte_set_bootloader();
      return true;
    case RebootMode::kRecovery:
      bootbyte_set_recovery();
      return true;
  }

  ZX_ASSERT(false);
}

RebootMode GetRebootMode() {
  unsigned char bootbyte = bootbyte_read() & ~RTC_BOOT_COUNT_MASK;
  if (bootbyte == RTC_BOOT_NORMAL || bootbyte == RTC_BOOT_DEFAULT) {
    return RebootMode::kNormal;
  } else if (bootbyte == RTC_BOOT_BOOTLOADER) {
    return RebootMode::kBootloader;
  } else if (bootbyte == RTC_BOOT_RECOVERY) {
    return RebootMode::kRecovery;
  } else {
    printf("Unknown bootmode: 0x%x. Doing normal boot\n", bootbyte);
    return RebootMode::kNormal;
  }
}

}  // namespace gigaboot
