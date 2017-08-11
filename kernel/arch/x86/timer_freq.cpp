// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/x86.h>
#include <arch/x86/feature.h>
#include <arch/x86/timer_freq.h>
#include <bits.h>
#include <magenta/errors.h>

#define MSR_PLATFORM_INFO 0xCE

static uint64_t compute_p_state_clock(uint64_t p_state_msr) {
    // is it valid?
    if (!BIT(p_state_msr, 63))
        return 0;

    // different AMD microarchitectures use slightly different formulas to compute
    // the effective clock rate of a P state
    uint64_t clock = 0;
    switch (x86_microarch) {
        case X86_MICROARCH_AMD_BULLDOZER:
        case X86_MICROARCH_AMD_JAGUAR: {
            uint64_t did = BITS_SHIFT(p_state_msr, 8, 6);
            uint64_t fid = BITS(p_state_msr, 5, 0);

            clock = (100 * (fid + 0x10) / (1 << did)) * 1000 * 1000;
            break;
        }
        case X86_MICROARCH_AMD_ZEN: {
            uint64_t fid = BITS(p_state_msr, 7, 0);

            clock = (fid * 25) * 1000 * 1000;
            break;
        }
        default:
            break;
    }

    return clock;
}

uint64_t x86_lookup_core_crystal_freq() {
    // The APIC frequency is the core crystal clock frequency if it is
    // enumerated in the CPUID leaf 0x15, or the processor's bus clock
    // frequency.
    const struct cpuid_leaf *tsc_leaf = x86_get_cpuid_leaf(X86_CPUID_TSC);
    if (tsc_leaf && tsc_leaf->c != 0) {
        return tsc_leaf->c;
    }

    switch (x86_microarch) {
        case X86_MICROARCH_INTEL_SKYLAKE:
        case X86_MICROARCH_INTEL_KABYLAKE:
            return 24u * 1000 * 1000;
        case X86_MICROARCH_INTEL_SANDY_BRIDGE:
        case X86_MICROARCH_INTEL_IVY_BRIDGE:
        case X86_MICROARCH_INTEL_HASWELL:
        case X86_MICROARCH_INTEL_BROADWELL: {
            uint64_t platform_info;
            if (read_msr_safe(MSR_PLATFORM_INFO, &platform_info) == MX_OK) {
                uint64_t bus_freq_mult = (platform_info >> 8) & 0xf;
                return bus_freq_mult * 100 * 1000 * 1000;
            }
            break;
        }
        case X86_MICROARCH_AMD_BULLDOZER:
        case X86_MICROARCH_AMD_JAGUAR:
        case X86_MICROARCH_AMD_ZEN:
            // 15h-17h BKDGs mention the APIC timer rate is 2xCLKIN,
            // which experimentally appears to be 100Mhz always
            return 100 * 1000 * 1000;
        case X86_MICROARCH_UNKNOWN:
            break;
    }

    return 0;
}

uint64_t x86_lookup_tsc_freq() {
    if (x86_vendor == X86_VENDOR_INTEL) {
        const uint64_t core_crystal_clock_freq = x86_lookup_core_crystal_freq();

        // If this leaf is present, then 18.18.3 (Determining the Processor Base
        // Frequency) documents this as the nominal TSC frequency.
        const struct cpuid_leaf *tsc_leaf = x86_get_cpuid_leaf(X86_CPUID_TSC);
        if (tsc_leaf && tsc_leaf->a) {
            return (core_crystal_clock_freq * tsc_leaf->b) / tsc_leaf->a;
        }
    } else if (x86_vendor == X86_VENDOR_AMD) {
        uint32_t p0_state_msr = 0xc0010064; // base P-state MSR
        switch (x86_microarch) {
            // TSC is invariant on these architectures and defined as running at
            // P0 clock, so compute it
            case X86_MICROARCH_AMD_ZEN: {
                // According to the Family 17h PPR, the first P-state MSR is indeed
                // P0 state and appears to be experimentally so
                uint64_t p0_state;
                if (read_msr_safe(p0_state_msr, &p0_state) != MX_OK)
                    break;

                return compute_p_state_clock(p0_state);
            }
            default:
                break;
        }
    }

    return 0;
}
