// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/x86.h>
#include <arch/x86/feature.h>
#include <arch/x86/tsc.h>

#define X86_MSR_IA32_TIME_STAMP_COUNTER 0x10

uint64_t tsc_adj = 0;

void x86_tsc_adjust(void) {
    if (x86_feature_test(X86_FEATURE_TSC_ADJUST))
        write_msr(X86_MSR_IA32_TSC_ADJUST, tsc_adj);
}

void x86_tsc_store_adjustment(void) {
    if (x86_feature_test(X86_FEATURE_TSC_ADJUST))
        tsc_adj = read_msr(X86_MSR_IA32_TIME_STAMP_COUNTER);
}
