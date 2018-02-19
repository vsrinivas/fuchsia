// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/x86/feature.h>
#include <arch/x86/pvclock.h>
#include <arch/x86/timer_freq.h>

uint64_t x86_lookup_core_crystal_freq() {
    return x86_get_microarch_config()->get_apic_freq();
}

uint64_t x86_lookup_tsc_freq() {
    if (pvclock_is_present()) {
        return pvclock_get_tsc_freq();
    }
    return x86_get_microarch_config()->get_tsc_freq();
}
