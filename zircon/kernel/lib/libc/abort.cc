// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdlib.h>
#include <zircon/assert.h>

void abort() { ZX_PANIC("abort() called!"); }

// The compiler generates calls to this for -fstack-protector.
extern "C" void __stack_chk_fail();

extern "C" void __stack_chk_fail() {
  // By trapping instead of panicking, we'll preserve more register state and
  // the exception handler will dump that state to the serial port or crash log.
  // If we're lucky the registers will still contain both the actual and
  // expected stack guard values so we can disambiguate stack corruption from
  // arch_thread / percpu struct corruption.
  __builtin_trap();
}
