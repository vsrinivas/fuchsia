// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_PC_SMBIOS_H_
#define ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_PC_SMBIOS_H_

#include <lib/smbios/smbios.h>

void pc_init_smbios();
zx_paddr_t pc_get_smbios_entrypoint();

// Walk the known SMBIOS structures.  The callback will be called once for each
// structure found.
zx_status_t SmbiosWalkStructs(smbios::StructWalkCallback cb);

#endif  // ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_PC_SMBIOS_H_
