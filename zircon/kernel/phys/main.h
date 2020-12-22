// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_MAIN_H_
#define ZIRCON_KERNEL_PHYS_MAIN_H_

#include <lib/arch/ticks.h>
#include <zircon/compiler.h>

#if defined(ZX_STATIC_PIE)
#include <lib/static-pie/static-pie.h>
#endif

// There's never any such object but the static analysis API requires some
// C++ object to refer to.  The PHYS_SINGLETHREAD marker on any function
// asserts that it is only run in the single thread rooted at PhysMain.
//
// TODO(mcgrathr): If and when code developed for phys environments is also
// shared in multithreaded kernel or user environments then this should
// probably be replaced by a lock/guard template wrapper regime that uses
// specific static analysis annotations and in fancy environments is real
// locks but in single-threaded environments is compiled away dummy lock
// types that only do the static analysis.  For now, we take pains to mark
// specific reusable-looking code with PHYS_SINGLETHREAD any place that it
// has single-threaded assumptions.
namespace PhysMainSingleThread {
struct __TA_CAPABILITY("PhysMainSingleThread") Type {};
constexpr Type kInstance;
}  // namespace PhysMainSingleThread
#define PHYS_SINGLETHREAD __TA_REQUIRES(PhysMainSingleThread::kInstance)

// This is the entry point from the assembly code kernel entry point.
// The stack and thread pointer ABIs are fully set up for normal C++ code.
// The first argument is passed along from the boot loader and the second
// is the earliest possible time sample at entry.
extern "C" [[noreturn]] void PhysMain(void*, arch::EarlyTicks) PHYS_SINGLETHREAD;

// In ZBI executables, PhysMain is defined to set up the console on stdout and
// then hand off to ZbiMain.  So ZbiMain is the main entry point that a ZBI
// executable defines.  It can use printf (and stdout generally) freely.
[[noreturn]] void ZbiMain(void* zbi, arch::EarlyTicks) PHYS_SINGLETHREAD;

// These are defined by the linker script.
extern "C" __LOCAL const char PHYS_LOAD_ADDRESS[];  // Address this file was loaded into memory.
extern "C" __LOCAL const char _end[];               // End of the image, including ".bss"

// Apply any relocations to our binary.
//
// This is a no-op on binaries linked to a fixed location, but is required for
// binaries compiled as position-independent to ensure pointers in data sections,
// vtables, etc, are updated to their correct locations.
inline void ApplyRelocations() {
#if defined(ZX_STATIC_PIE)
  // If we are position-independent, apply any simple fixups required.
  static_pie::ApplyDynamicRelocationsToSelf(reinterpret_cast<uintptr_t>(PHYS_LOAD_ADDRESS));
#endif
}

#endif  // ZIRCON_KERNEL_PHYS_MAIN_H_
