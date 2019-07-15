// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_DEV_POWER_H_
#define ZIRCON_KERNEL_INCLUDE_DEV_POWER_H_

#include <zircon/compiler.h>

__BEGIN_CDECLS

enum reboot_flags {
  REBOOT_NORMAL = 0,
  REBOOT_BOOTLOADER = 1,
  REBOOT_RECOVERY = 2,
};

void power_reboot(enum reboot_flags flags);
void power_shutdown(void);

__END_CDECLS

#endif  // ZIRCON_KERNEL_INCLUDE_DEV_POWER_H_
