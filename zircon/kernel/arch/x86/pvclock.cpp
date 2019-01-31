// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/ops.h>
#include <arch/x86.h>
#include <arch/x86/feature.h>
#include <arch/x86/pvclock.h>
#include <kernel/atomic.h>
#include <vm/physmap.h>
#include <vm/pmm.h>

static volatile struct pvclock_boot_time* boot_time = nullptr;
static volatile struct pvclock_system_time* system_time = nullptr;

static constexpr uint64_t kSystemTimeEnable = 1u;

zx_status_t pvclock_init(void) {
    if (boot_time != nullptr || system_time != nullptr) {
        return ZX_ERR_BAD_STATE;
    }

    paddr_t pa;
    zx_status_t status = pmm_alloc_page(0, &pa);
    if (status != ZX_OK) {
        return status;
    }
    arch_zero_page(paddr_to_physmap(pa));
    boot_time = static_cast<struct pvclock_boot_time*>(paddr_to_physmap(pa));
    write_msr(kKvmBootTime, pa);

    status = pmm_alloc_page(0, &pa);
    if (status != ZX_OK) {
        return status;
    }
    arch_zero_page(paddr_to_physmap(pa));
    system_time = static_cast<struct pvclock_system_time*>(paddr_to_physmap(pa));
    write_msr(kKvmSystemTimeMsr, pa | kSystemTimeEnable);

    return ZX_OK;
}

bool pvclock_is_present(void) {
    if (x86_hypervisor != X86_HYPERVISOR_KVM) {
        return false;
    }
    uint32_t a, ignored;
    cpuid(X86_CPUID_KVM_FEATURES, &a, &ignored, &ignored, &ignored);
    if (a & kKvmFeatureClockSource) {
        return true;
    }
    return false;
}

bool pvclock_is_stable() {
    bool is_stable = (system_time->flags & kKvmSystemTimeStable) ||
                     x86_feature_test(X86_FEATURE_KVM_PVCLOCK_STABLE);
    printf("pvclock: Clocksource is %sstable\n", (is_stable ? "" : "not "));
    return is_stable;
}

uint64_t pvclock_get_tsc_freq() {
    printf("pvclock: Fetching TSC frequency\n");
    uint32_t tsc_mul = 0;
    int8_t tsc_shift = 0;
    uint32_t pre_version = 0, post_version = 0;
    do {
        pre_version = atomic_load_u32(&system_time->version);
        if (pre_version % 2 != 0) {
            arch_spinloop_pause();
            continue;
        }
        tsc_mul = system_time->tsc_mul;
        tsc_shift = system_time->tsc_shift;
        post_version = atomic_load_u32(&system_time->version);
    } while (pre_version != post_version);

    uint64_t tsc_khz = 1000000ULL << 32;
    tsc_khz = tsc_khz / tsc_mul;
    if (tsc_shift > 0) {
        tsc_khz >>= tsc_shift;
    } else {
        tsc_khz <<= -tsc_shift;
    }
    return tsc_khz * 1000;
}
