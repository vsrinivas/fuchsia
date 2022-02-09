// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_HWREG_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_HWREG_H_

#include <hwreg/bitfields.h>

namespace arch {

// Various hwreg types declared in <lib/arch/...> headers use the template
// argument arch::EnablePrinter in place of hwreg::EnablePrinter.  The hwreg
// printing support has an unreasonable runtime cost for the non-printing uses.
// In particular, it makes the hwreg objects very large and need runtime
// construction, as oppoosed to being tiny one-word objects whose construction
// is inlined away.  This adds setup overhead to code using the types, but more
// importantly makes functions using several such objects get unreasonable
// stack sizes very quickly.  So it's conditionally compiled in only in places
// that want that support, which is generally only useful in build-time and
// test code and isn't used in production code where keeping the hwreg types
// optimal is important.  Hence, this should not be enabled when compiling code
// for the kernel or any such place, but only in test-only code.

#ifdef LIB_ARCH_PRINTERS
using EnablePrinter = hwreg::EnablePrinter;
#else
using EnablePrinter = void;
#endif

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_HWREG_H_
