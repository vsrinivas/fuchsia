// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

/*
 * Use a pragma to set all objects to hidden, the command-line option does not
 * apply to externs.
 */
#pragma GCC visibility push(hidden)

#ifndef __clang__
// With GCC, -fasynchronous-unwind-tables emits full .eh_frame and no
// .debug_frame.  Just -fno-unwind-tables is actually independent and
// doesn't negate the default -fasynchronous-unwind-tables.  However,
// -fno-asynchronous-unwind-tables -fno-unwind-tables not only makes it
// emit .debug_frame instead of .eh_frame but also makes it emit partial
// (call-site-only) .debug_frame.  Since GCC just emits `.cfi_*` asm
// directives and doesn't otherwise emit its own `.cfi_sections`, we can
// just inject one early here (see kernel/debug-frame.S for where the
// corresponding trick is done for assembly files).
__asm__(".cfi_sections .debug_frame");
#endif
