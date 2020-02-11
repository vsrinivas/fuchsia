// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>
#include <zircon/time.h>

#include "private.h"

// Generate the three versions of zx_deadline_after which we may select.
//
// The first uses the default version of zx_clock_get_monotonic, which accesses
// the tick source directly from user mode, and then scales the number it gets
// from that using the ticks to mono ration published in the rodata of the vdso.
//
// The second uses the explicit syscall version of zx_clock_get_monotonic.  This
// is the version which will be selected in the evet that get_monotonic *must*
// be a syscall.
//
// The final version uses a version of zx_clock_get_monotonic which fetches the
// ticks reference using a syscall, but then scales the results using the
// published ratio.  This is the version which will be used if the ticks
// reference is inaccessible from user mode.
//
// See comments in zx_clock_monotonic.cc for additional details.
//
__EXPORT zx_time_t _zx_deadline_after(zx_duration_t nanoseconds) {
  zx_time_t now = VDSO_zx_clock_get_monotonic();
  return zx_time_add_duration(now, nanoseconds);
}

VDSO_KERNEL_EXPORT zx_time_t CODE_deadline_after_via_kernel_mono(zx_duration_t nanoseconds) {
  zx_time_t now = SYSCALL_zx_clock_get_monotonic_via_kernel();
  return zx_time_add_duration(now, nanoseconds);
}

VDSO_KERNEL_EXPORT zx_time_t CODE_deadline_after_via_kernel_ticks(zx_duration_t nanoseconds) {
  zx_time_t now = CODE_clock_get_monotonic_via_kernel_ticks();
  return zx_time_add_duration(now, nanoseconds);
}

VDSO_INTERFACE_FUNCTION(zx_deadline_after);
