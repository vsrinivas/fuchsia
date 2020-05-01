// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "debug.h"

#include <align.h>
#include <ctype.h>
#include <platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/boot/crash-reason.h>
#include <zircon/listnode.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <arch/ops.h>
#include <dev/hw_rng.h>
#include <kernel/spinlock.h>
#include <ktl/algorithm.h>
#include <platform/debug.h>

void spin(uint32_t usecs) {
  zx_time_t start = current_time();

  zx_duration_t nsecs = ZX_USEC(usecs);
  while (zx_time_sub_time(current_time(), start) < nsecs)
    ;
}

void panic(const char* fmt, ...) {
  platform_panic_start();

  // Print the program counter of our caller (i.e., the program counter of
  // of the "call panic" instruction) and the current stack frame pointer.
  printf(
      "\n"
      "*** KERNEL PANIC (caller pc: %p, stack frame: %p):\n"
      "*** ",
      __GET_CALLER(), __GET_FRAME());

  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);

  // Add a newline to the end of the panic message if it was missing.
  size_t len = strlen(fmt);
  if (len == 0 || fmt[len - 1] != '\n') {
    printf("\n");
  }

  // Leave a blank line between the panic message and the to-be-printed backtrace.
  printf("\n");

  platform_halt(HALT_ACTION_HALT, ZirconCrashReason::Panic);
}

__NO_SAFESTACK uintptr_t choose_stack_guard(void) {
  uintptr_t guard;
  if (hw_rng_get_entropy(&guard, sizeof(guard)) != sizeof(guard)) {
    // We can't get a random value, so use a randomish value.
    guard = 0xdeadbeef00ff00ffUL ^ (uintptr_t)&guard;
  }
  return guard;
}
