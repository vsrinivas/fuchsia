// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fenv.h>
#include <stdint.h>
#include <zircon/compiler.h>

int fegetround(void) {
  uint64_t value;
  __asm__("frrm %0" : "=r"(value));
  return value;
}

__LOCAL int __fesetround(int round) {
  __asm__("fsrm t0, %0" : "=r"(round));
  return 0;
}

int feclearexcept(int mask) {
  __asm__("csrc fflags, %0" : "=r"(mask));
  return 0;
}

int feraiseexcept(int mask) {
  __asm__("csrs fflags, %0" : "=r"(mask));
  return 0;
}

int fetestexcept(int mask) {
  int value = 0;
  __asm__("frflags %0" : "=r"(value));
  return value & mask;
}

int fegetenv(fenv_t* env) {
  int value = 0;
  __asm__("fscsr %0" : "=r"(value));
  *env = value;
  return 0;
}

int fesetenv(const fenv_t* env) {
  int value = 0;
  if (env != FE_DFL_ENV) {
    value = *env;
  }
  __asm__("fscsr %0" : "=r"(value));
  return 0;
}
