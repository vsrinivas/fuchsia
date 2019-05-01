// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file defines all the entry points that -fsanitize-coverage=...
// instrumentation calls.  Unfortunately, LLVM does not publish any header
// file declaring those signatures, though they are all given in
// compiler-rt/lib/sanitizer_common/sanitizer_interface_internal.h.
//
// The definitions here are all weak symbols and they are just sufficient
// for any calls that might be made by the dynamic linker startup path
// before it has finished loading and relocating the actual coverage
// runtime provided in the executable or some shared library.  Definitions
// for everything that libc itself refers to must be provided here, even if
// they will never be reached at runtime.

#include <cstdint>

// This should never be called, because the runtime should have been
// loaded before any module initializers get called.
[[gnu::weak]] extern "C" void __sanitizer_cov_trace_pc_guard_init(uint32_t*,
                                                                  uint32_t*) {
    __builtin_trap();
}

// This is called only from __asan_early_init, which is the only thing
// called during dynamic linker startup before the runtime has been
// loaded that's outside dynlink.c, where _dynlink_sancov_trampoline
// short-circuits before calling here.  Just sanity-check that we
// aren't getting here after module initializers have run.
[[gnu::weak]] extern "C" void __sanitizer_cov_trace_pc_guard(uint32_t* guard) {
    if (*guard != 0) {
        __builtin_trap();
    }
}
