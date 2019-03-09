// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_SYSCALLS_TYPES_H_
#define SYSROOT_ZIRCON_SYSCALLS_TYPES_H_

#include <zircon/compiler.h>

__BEGIN_CDECLS

// forward declarations needed by syscalls.h
typedef struct zx_port_packet zx_port_packet_t;
typedef struct zx_pci_bar zx_pci_bar_t;
typedef struct zx_pcie_device_info zx_pcie_device_info_t;
typedef struct zx_pci_init_arg zx_pci_init_arg_t;
typedef union zx_rrec zx_rrec_t;
typedef struct zx_system_powerctl_arg zx_system_powerctl_arg_t;
typedef struct zx_profile_info zx_profile_info_t;
typedef struct zx_smc_parameters zx_smc_parameters_t;
typedef struct zx_smc_result zx_smc_result_t;

__END_CDECLS

#endif  // SYSROOT_ZIRCON_SYSCALLS_TYPES_H_
