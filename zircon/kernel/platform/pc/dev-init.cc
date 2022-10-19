// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/uart/all.h>

#include <dev/init.h>
#include <phys/arch/arch-handoff.h>
#include <platform/pc/debug.h>

void PlatformDriverHandoffEarly(const ArchPhysHandoff& arch_handoff) {}

void PlatformDriverHandoffLate(const ArchPhysHandoff& arch_handoff) {}

void PlatformUartDriverHandoffEarly(const uart::all::Driver& serial) { X86UartInitEarly(serial); }

void PlatformUartDriverHandoffLate(const uart::all::Driver& serial) { X86UartInitLate(); }
