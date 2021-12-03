// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/uart/all.h>

#include <dev/init.h>
#include <phys/arch/arch-handoff.h>
#include <platform/pc/debug.h>

void ArchDriverHandoffEarly(const ArchPhysHandoff& arch_handoff) {}

void ArchDriverHandoffLate(const ArchPhysHandoff& arch_handoff) {}

void ArchUartDriverHandoffEarly(const uart::all::Driver& serial) { X86UartInitEarly(serial); }

void ArchUartDriverHandoffLate(const uart::all::Driver& serial) { X86UartInitLate(); }
