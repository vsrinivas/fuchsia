// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_PHYS_BOOT_SHIM_STDOUT_H_
#define ZIRCON_KERNEL_ARCH_X86_PHYS_BOOT_SHIM_STDOUT_H_

#include <ktl/string_view.h>

// Parse kernel.serial=... from the command line to update stdout.
void StdoutFromCmdline(ktl::string_view cmdline);

#endif  // ZIRCON_KERNEL_ARCH_X86_PHYS_BOOT_SHIM_STDOUT_H_
