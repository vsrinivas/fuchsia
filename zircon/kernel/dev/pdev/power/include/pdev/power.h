// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_DEV_PDEV_POWER_INCLUDE_PDEV_POWER_H_
#define ZIRCON_KERNEL_DEV_PDEV_POWER_INCLUDE_PDEV_POWER_H_

#include <zircon/compiler.h>

#include <dev/power.h>

__BEGIN_CDECLS

// power interface
struct pdev_power_ops {
  void (*reboot)(enum reboot_flags flags);
  void (*shutdown)(void);
  uint32_t (*cpu_off)(void);
  uint32_t (*cpu_on)(uint64_t mpid, paddr_t entry);
};

void pdev_register_power(const struct pdev_power_ops* ops);

__END_CDECLS

#endif  // ZIRCON_KERNEL_DEV_PDEV_POWER_INCLUDE_PDEV_POWER_H_
