// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/x86/mp.h>
#include <arch/x86/apic.h>

// This is in its own file rather than mp.cpp so that it can
// be a C file and use C99 designated initializer syntax.

#if __has_feature(safe_stack)
static uint8_t unsafe_kstack[PAGE_SIZE] __ALIGNED(16);
#define unsafe_kstack_end (&unsafe_kstack[sizeof(unsafe_kstack)])
#else
#define unsafe_kstack_end NULL
#endif

struct x86_percpu bp_percpu = {
    .cpu_num = 0,
    .direct = &bp_percpu,
    .kernel_unsafe_sp = (uintptr_t)unsafe_kstack_end,

    // Start with an invalid ID until we know the local APIC is set up.
    .apic_id = INVALID_APIC_ID,
};
