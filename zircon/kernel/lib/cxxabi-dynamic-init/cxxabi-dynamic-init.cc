// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lib/cxxabi-dynamic-init/cxxabi-dynamic-init.h"

#include <stdint.h>

extern "C" int __cxa_guard_acquire(uint64_t* guard_object) {
  return cxxabi_dynamic_init::Acquire(guard_object);
}

extern "C" void __cxa_guard_release(uint64_t* guard_object) {
  cxxabi_dynamic_init::Release(guard_object);
}

extern "C" void __cxa_guard_abort(uint64_t* guard_object) {
  cxxabi_dynamic_init::Abort(guard_object);
}
