// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_DEV_INIT_H_
#define ZIRCON_KERNEL_INCLUDE_DEV_INIT_H_

#include <lib/uart/all.h>

// Forward-declared; fully declared in <phys/handoff.h>
struct PhysHandoff;

// Forward-declared; fully declared in <phys/arch/arch-handoff.h>.
struct ArchPhysHandoff;

// These routines initialize the kernel's drivers, as a function of the
// configurations described in the physboot hand-off, at two stages:
// * "early" refers to immediately after early platform initialization, before
//   the VM and heap are set up;
// * "late" refers to immediately after the main platform initialization, once
//   the VM, the heap, threading, and general kernel facilities are available.
//
// Defined in //zircon/kernel/dev/init.cc.
void DriverHandoffEarly(const PhysHandoff& handoff);
void DriverHandoffLate(const PhysHandoff& handoff);

// Platform-specific subroutines of the above functions, respectively.
//
// Defined in //zircon/kernel/platform/*/dev-init.cc.
void PlatformDriverHandoffEarly(const ArchPhysHandoff& arch_handoff);
void PlatformDriverHandoffLate(const ArchPhysHandoff& arch_handoff);

// Further platform-specific subroutines for the UART.
//
// TODO(fxbug.dev/89182): These will go away when the UART driver can dealt
// with directly as a platform-agnostic libuart type.
void PlatformUartDriverHandoffEarly(const uart::all::Driver& serial);
void PlatformUartDriverHandoffLate(const uart::all::Driver& serial);

#endif  // ZIRCON_KERNEL_INCLUDE_DEV_INIT_H_
