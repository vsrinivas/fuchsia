// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_PHYS_BOOT_SHIM_STDOUT_H_
#define ZIRCON_KERNEL_ARCH_X86_PHYS_BOOT_SHIM_STDOUT_H_

#include <lib/uart/all.h>

#include <ktl/string_view.h>

// If |cmdline| provides 'kernel.serial=' override |uart| is replaced by the provided configuration.
// Otherwise, |uart| is left unchanged.
void UartFromCmdLine(ktl::string_view cmdline, uart::all::Driver& uart);

#endif  // ZIRCON_KERNEL_ARCH_X86_PHYS_BOOT_SHIM_STDOUT_H_
