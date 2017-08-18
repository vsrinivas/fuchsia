// Copyright 2017 The Fuchsia Authors
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/compiler.h>
#include <err.h>
#include <inttypes.h>
#include <string.h>
#include <arch/x86/feature.h>
#include <kernel/auto_lock.h>
#include <kernel/mp.h>
#include <lib/console.h>

static bool hwp_enabled = false;

static spin_lock_t lock = SPIN_LOCK_INITIAL_VALUE;

static void hwp_enable_sync_task(void* ctx)
{
    // Enable HWP
    write_msr(X86_MSR_IA32_PM_ENABLE, 1);

    // 14.4.7 set minimum/maximum to values from capabilities for
    // common case. hint=0x80 by default
    uint64_t hwp_caps = read_msr(X86_MSR_IA32_HWP_CAPABILITIES);
    uint64_t hwp_req = (0x80ull << 24) | ((hwp_caps & 0xff) << 8) | ((hwp_caps >> 24) & 0xff);
    write_msr(X86_MSR_IA32_HWP_REQUEST, hwp_req);
}

static void hwp_enable(void)
{
    AutoSpinLock guard(&lock);

    if (hwp_enabled) {
        return;
    }

    if (!x86_feature_test(X86_FEATURE_HWP)) {
        printf("HWP not supported\n");
        return;
    }

    mp_sync_exec(MP_IPI_TARGET_ALL, 0, hwp_enable_sync_task, NULL);

    hwp_enabled = true;
}

static void hwp_set_hint_sync_task(void* ctx)
{
    uint8_t hint = (unsigned long)ctx & 0xff;
    uint64_t hwp_req = read_msr(X86_MSR_IA32_HWP_REQUEST) & ~(0xff << 24);
    hwp_req |= (hint << 24);
    hwp_req &= ~(0xffffffffull << 32);
    write_msr(X86_MSR_IA32_HWP_REQUEST, hwp_req);
}

static void hwp_set_hint(unsigned long hint) {
    AutoSpinLock guard(&lock);

    if (!hwp_enabled) {
        printf("Enable HWP first\n");
        return;
    }
    if (!x86_feature_test(X86_FEATURE_HWP_PREF)) {
        printf("HWP hint not supported\n");
        return;
    }
    mp_sync_exec(MP_IPI_TARGET_ALL, 0, hwp_set_hint_sync_task, (void*)hint);
}

static int cmd_hwp(int argc, const cmd_args *argv, uint32_t flags)
{
    if (argc < 2) {
notenoughargs:
        printf("not enough arguments\n");
usage:
        printf("usage:\n");
        printf("%s enable\n", argv[0].str);
        printf("%s hint <0-255>\n", argv[0].str);
        return MX_ERR_INTERNAL;
    }

    if (!strcmp(argv[1].str, "enable")) {
        hwp_enable();
    } else if (!strcmp(argv[1].str, "hint")) {
        if (argc < 3) {
            goto notenoughargs;
        }
        if (argv[2].u > 0xff) {
            printf("hint must be between 0 (performance) and 255 (energy efficiency)!");
            goto usage;
        }
        hwp_set_hint(argv[2].u);
    } else {
        printf("unknown command\n");
        goto usage;
    }

    return MX_OK;
}

STATIC_COMMAND_START
STATIC_COMMAND("hwp", "hardware controlled performance states\n", &cmd_hwp)
STATIC_COMMAND_END(hwp);
