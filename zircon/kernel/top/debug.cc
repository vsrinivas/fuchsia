// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "debug.h"

#include <align.h>
#include <ctype.h>
#include <lib/crashlog.h>
#include <platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/boot/crash-reason.h>
#include <zircon/listnode.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <arch/ops.h>
#include <dev/hw_rng.h>
#include <kernel/lockdep.h>
#include <kernel/spinlock.h>
#include <ktl/algorithm.h>
#include <platform/debug.h>

namespace {

// Start a system panic, and print a header message.
//
// Calls should be followed by:
//
//   * Calling "printf" with the reason for the panic, followed by
//     a newline.
//
//   * A call to "PanicFinish".
__ALWAYS_INLINE inline void PanicStart(void* pc, void* frame) {
  platform_panic_start();

  fprintf(&stdout_panic_buffer,
          "\n"
          "*** KERNEL PANIC (caller pc: %p, stack frame: %p):\n"
          "*** ",
          pc, frame);
}

// Finish a system panic.
//
// This function will not return, but will perform an action such as
// rebooting the system or dropping the system into a debug shell.
//
// Marked "__ALWAYS_INLINE" to avoid an additional stack frame from
// appearing in the backtrace.
__ALWAYS_INLINE __NO_RETURN inline void PanicFinish() {
  // Add a newline between the panic message and the stack trace.
  fprintf(&stdout_panic_buffer, "\n");

  platform_halt(HALT_ACTION_HALT, ZirconCrashReason::Panic);
}

// Determine if the given string ends with the given character.
bool EndsWith(const char* str, char x) {
  size_t len = strlen(str);
  return len > 0 && str[len - 1] == x;
}

}  // namespace

void spin(uint32_t usecs) {
  zx_time_t start = current_time();

  zx_duration_t nsecs = ZX_USEC(usecs);
  while (zx_time_sub_time(current_time(), start) < nsecs)
    ;
}

void panic(const char* fmt, ...) {
  PanicStart(__GET_CALLER(), __GET_FRAME());

  // Print the user message.
  va_list ap;
  va_start(ap, fmt);
  vfprintf(&stdout_panic_buffer, fmt, ap);
  va_end(ap);

  // Add a newline to the end of the panic message if it was missing.
  if (!EndsWith(fmt, '\n')) {
    fprintf(&stdout_panic_buffer, "\n");
  }

  PanicFinish();
}

void assert_fail_msg(const char* file, int line, const char* expression, const char* fmt, ...) {
  PanicStart(__GET_CALLER(), __GET_FRAME());

  // Print the user message.
  fprintf(&stdout_panic_buffer, "ASSERT FAILED at (%s:%d): %s\n", file, line, expression);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(&stdout_panic_buffer, fmt, ap);
  va_end(ap);

  // Add a newline to the end of the panic message if it was missing.
  if (!EndsWith(fmt, '\n')) {
    fprintf(&stdout_panic_buffer, "\n");
  }

  PanicFinish();
}

void assert_fail(const char* file, int line, const char* expression) {
  PanicStart(__GET_CALLER(), __GET_FRAME());
  fprintf(&stdout_panic_buffer, "ASSERT FAILED at (%s:%d): %s\n", file, line, expression);
  PanicFinish();
}

__NO_SAFESTACK uintptr_t choose_stack_guard(void) {
  uintptr_t guard;
  if (hw_rng_get_entropy(&guard, sizeof(guard)) != sizeof(guard)) {
    // We can't get a random value, so use a randomish value.
    guard = 0xdeadbeef00ff00ffUL ^ (uintptr_t)&guard;
  }
  return guard;
}
