/*
 * Copyright 2018 The Fuchsia Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * This is an input linker script used in linking the vDSO.
 * The kernel can switch vDSO symbols at boot time to alternate entry points.
 * The alternate code is at hidden symbols.  The linker will GC these
 * entry points without some reference chain from an exported symbol.
 * This file injects synthetic references to keep the symbols alive.
 */

EXTERN(CODE_clock_get_monotonic_via_kernel_ticks)
EXTERN(CODE_deadline_after_via_kernel_mono)
EXTERN(CODE_deadline_after_via_kernel_ticks)
EXTERN(CODE_ticks_get_arm_a73)
