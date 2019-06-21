// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/x86.h>
#include <arch/x86/feature.h>

uint32_t x86_amd_get_patch_level(void) {
    uint32_t patch_level = 0;
    if (!x86_feature_test(X86_FEATURE_HYPERVISOR)) {
        patch_level = static_cast<uint32_t>(read_msr(X86_MSR_IA32_BIOS_SIGN_ID));
    }
    return patch_level;
}

void x86_amd_init_percpu(void) {
}
