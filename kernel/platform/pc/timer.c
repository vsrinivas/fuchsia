// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <sys/types.h>

#include <assert.h>
#include <debug.h>
#include <err.h>
#include <reg.h>
#include <trace.h>

#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <arch/x86/feature.h>
#include <lib/fixed_point.h>
#include <lk/init.h>
#include <kernel/cmdline.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <platform.h>
#include <dev/interrupt.h>
#include <platform/console.h>
#include <platform/pc.h>
#include <platform/pc/acpi.h>
#include <platform/pc/hpet.h>
#include <platform/timer.h>
#include "platform_p.h"

// Current timer scheme:
// The HPET is used to calibrate the local APIC timers and the TSC.  If the
// HPET is not present, we will fallback to calibrating using the PIT.
//
// For wall-time, we use the following mechanisms, in order of highest
// preference to least:
// 1) TSC: If the CPU advertises an invariant TSC, then we will use the TSC for
// tracking wall time in a tickless manner.
// 2) HPET: If there is an HPET present, we will use its count to track wall
// time in a tickless manner.
// 3) PIT: We will use periodic interrupts to update wall time.
//
// The local APICs are responsible for handling timer callbacks
// sent from the scheduler.

enum clock_source {
    CLOCK_PIT,
    CLOCK_HPET,
    CLOCK_TSC,

    CLOCK_COUNT
};

const char *clock_name[] = {
    [CLOCK_PIT] = "PIT",
    [CLOCK_HPET] = "HPET",
    [CLOCK_TSC] = "TSC",
};
static_assert(countof(clock_name) == CLOCK_COUNT, "");


static platform_timer_callback t_callback[SMP_MAX_CPUS] = {NULL};
static void *callback_arg[SMP_MAX_CPUS] = {NULL};

// PIT time accounting info
static uint64_t next_trigger_time;
static uint64_t next_trigger_delta;
static uint64_t timer_delta_time;
static volatile uint64_t timer_current_time;
static uint16_t pit_divisor;

// Whether or not we have an Invariant TSC (controls whether we use the PIT or
// not after initialization).  The Invariant TSC is rate-invariant under P-, C-,
// and T-state transitions.
static bool invariant_tsc;
// Whether or not we have a Constant TSC (controls whether we bother calibrating
// the TSC).  Constant TSC predates the Invariant TSC.  The Constant TSC is
// rate-invariant under P-state transitions.
static bool constant_tsc;

static enum clock_source wall_clock;
static enum clock_source calibration_clock;

// APIC timer calibration values
static bool use_tsc_deadline;
static volatile uint32_t apic_ticks_per_ms = 0;
static uint8_t apic_divisor = 0;

// TSC timer calibration values
static uint64_t tsc_ticks_per_ms;
static struct fp_32_64 ns_per_tsc;

// HPET calibration values
static struct fp_32_64 ns_per_hpet;

// TODO: move this to a common header
uint64_t get_tsc_ticks_per_ms(void);
uint64_t get_tsc_ticks_per_ms(void) {
    return tsc_ticks_per_ms;
}

#define INTERNAL_FREQ 1193182ULL
#define INTERNAL_FREQ_3X 3579546ULL

#define INTERNAL_FREQ_TICKS_PER_MS (INTERNAL_FREQ/1000)

/* Maximum amount of time that can be program on the timer to schedule the next
 *  interrupt, in miliseconds */
#define MAX_TIMER_INTERVAL 55

#define LOCAL_TRACE 0

lk_time_t current_time(void)
{
    lk_time_t time;

    switch (wall_clock) {
        case CLOCK_TSC: {
            uint64_t tsc = rdtsc();
            time = tsc / tsc_ticks_per_ms;
            break;
        }
        case CLOCK_HPET: {
            uint64_t counter = hpet_get_value();
            time = counter / hpet_ticks_per_ms();
            break;
        }
        case CLOCK_PIT: {
            // XXX slight race
            time = (lk_time_t) (timer_current_time >> 32);
            break;
        }
        default:
            panic("Invalid wall clock source\n");
    }

    return time;
}

lk_bigtime_t current_time_hires(void)
{
    lk_bigtime_t time;

    switch (wall_clock) {
        case CLOCK_TSC: {
            uint64_t tsc = rdtsc();
            time = u64_mul_u64_fp32_64(tsc, ns_per_tsc);
            break;
        }
        case CLOCK_HPET: {
            uint64_t counter = hpet_get_value();
            time = u64_mul_u64_fp32_64(counter, ns_per_hpet);
            break;
        }
        case CLOCK_PIT: {
            // XXX slight race
            time = (lk_bigtime_t) ((timer_current_time >> 22) * 1000) >> 10;
            time *= 1000;
            break;
        }
        default:
            panic("Invalid wall clock source\n");
    }

    return time;
}

// The PIT timer will keep track of wall time if we aren't using the TSC
static enum handler_return pit_timer_tick(void *arg)
{
    timer_current_time += timer_delta_time;
    return INT_NO_RESCHEDULE;
}

// The APIC timers will call this when they fire
enum handler_return platform_handle_apic_timer_tick(void) {
    DEBUG_ASSERT(arch_ints_disabled());
    uint cpu = arch_curr_cpu_num();

    lk_time_t time = current_time();
    //lk_bigtime_t btime = current_time_hires();
    //printf_xy(71, 0, WHITE, "%08u", (uint32_t) time);
    //printf_xy(63, 1, WHITE, "%016llu", (uint64_t) btime);

    if (t_callback[cpu] && timer_current_time >= next_trigger_time) {
        lk_time_t delta = timer_current_time - next_trigger_time;
        next_trigger_time = timer_current_time + next_trigger_delta - delta;

        return t_callback[cpu](callback_arg[cpu], time);
    } else {
        return INT_NO_RESCHEDULE;
    }
}

static void set_pit_frequency(uint32_t frequency)
{
    uint32_t count, remainder;

    /* figure out the correct pit_divisor for the desired frequency */
    if (frequency <= 18) {
        count = 0xffff;
    } else if (frequency >= INTERNAL_FREQ) {
        count = 1;
    } else {
        count = INTERNAL_FREQ_3X / frequency;
        remainder = INTERNAL_FREQ_3X % frequency;

        if (remainder >= INTERNAL_FREQ_3X / 2) {
            count += 1;
        }

        count /= 3;
        remainder = count % 3;

        if (remainder >= 1) {
            count += 1;
        }
    }

    pit_divisor = count & 0xffff;

    /*
     * funky math that i don't feel like explaining. essentially 32.32 fixed
     * point representation of the configured timer delta.
     */
    timer_delta_time = (3685982306ULL * count) >> 10;

    //dprintf(DEBUG, "set_pit_frequency: dt=%016llx\n", timer_delta_time);
    //dprintf(DEBUG, "set_pit_frequency: pit_divisor=%04x\n", pit_divisor);

    /*
     * setup the Programmable Interval Timer
     * timer 0, mode 2, binary counter, LSB followed by MSB
     */
    outp(I8253_CONTROL_REG, 0x34);
    outp(I8253_DATA_REG, pit_divisor & 0xff); // LSB
    outp(I8253_DATA_REG, pit_divisor >> 8); // MSB
}

static inline void pit_calibration_cycle_preamble(uint16_t ms)
{
    // Make the PIT run for
    const uint16_t init_pic_count = INTERNAL_FREQ_TICKS_PER_MS * ms;
    // Program PIT in the interrupt on terminal count configuration,
    // this makes it count down and set the output high when it hits 0.
    outp(I8253_CONTROL_REG, 0x30);
    outp(I8253_DATA_REG, init_pic_count & 0xff); // LSB
}

static inline void pit_calibration_cycle(uint16_t ms)
{
    // Make the PIT run for ms millis, see comments in the preamble
    const uint16_t init_pic_count = INTERNAL_FREQ_TICKS_PER_MS * ms;
    outp(I8253_DATA_REG, init_pic_count >> 8); // MSB

    uint8_t status = 0;
    do {
        // Send a read-back command that latches the status of ch0
        outp(I8253_CONTROL_REG, 0xe2);
        status = inp(I8253_DATA_REG);
    // Wait for bit 7 (output) to go high and for bit 6 (null count) to go low
    } while ((status & 0xc0) != 0x80);
}

static inline void pit_calibration_cycle_cleanup(void)
{
    // Stop the PIT by starting a mode change but not writing a counter
    outp(I8253_CONTROL_REG, 0x38);
}

static inline void hpet_calibration_cycle_preamble(void)
{
    hpet_enable();
}

static inline void hpet_calibration_cycle(uint16_t ms)
{
    hpet_wait_ms(ms);
}

static inline void hpet_calibration_cycle_cleanup(void)
{
    hpet_disable();
}

static void calibrate_apic_timer(void)
{
    ASSERT(arch_ints_disabled());

    TRACEF("Calibrating APIC with %s\n", clock_name[calibration_clock]);

    apic_divisor = 1;
outer:
    while (apic_divisor != 0) {
        uint64_t best_time[2] = {UINT64_MAX, UINT64_MAX};
        const uint16_t duration_ms[2] = { 2, 4 };
        for (int trial = 0; trial < 2; ++trial) {
            for (int tries = 0; tries < 3; ++tries) {
                switch (calibration_clock) {
                    case CLOCK_HPET:
                        hpet_calibration_cycle_preamble();
                        break;
                    case CLOCK_PIT:
                        pit_calibration_cycle_preamble(duration_ms[trial]);
                        break;
                    default: PANIC_UNIMPLEMENTED;
                }

                // Setup APIC timer to count down with interrupt masked
                status_t status = apic_timer_set_oneshot(
                        UINT32_MAX,
                        apic_divisor,
                        true);
                ASSERT(status == NO_ERROR);

                switch (calibration_clock) {
                    case CLOCK_HPET:
                        hpet_calibration_cycle(duration_ms[trial]);
                        break;
                    case CLOCK_PIT:
                        pit_calibration_cycle(duration_ms[trial]);
                        break;
                    default: PANIC_UNIMPLEMENTED;
                }

                uint32_t apic_ticks = UINT32_MAX - apic_timer_current_count();
                if (apic_ticks < best_time[trial]) {
                    best_time[trial] = apic_ticks;
                }
                LTRACEF("Calibration trial %d found %u ticks/ms\n",
                        tries, apic_ticks);

                switch (calibration_clock) {
                    case CLOCK_HPET:
                        hpet_calibration_cycle_cleanup();
                        break;
                    case CLOCK_PIT:
                        pit_calibration_cycle_cleanup();
                        break;
                    default: PANIC_UNIMPLEMENTED;
                }
            }

            // If the APIC ran out of time every time, try again with a higher
            // divisor
            if (best_time[trial] == UINT32_MAX) {
                apic_divisor *= 2;
                goto outer;
            }

        }
        apic_ticks_per_ms = (best_time[1] - best_time[0]) / (duration_ms[1] - duration_ms[0]);
        break;
    }
    ASSERT(apic_divisor != 0);

    LTRACEF("APIC timer calibrated: %u ticks/ms, %d divisor\n",
            apic_ticks_per_ms, apic_divisor);
}

static void calibrate_tsc(void)
{
    ASSERT(arch_ints_disabled());

    TRACEF("Calibrating TSC with %s\n", clock_name[calibration_clock]);

    uint64_t best_time[2] = {UINT64_MAX, UINT64_MAX};
    const uint16_t duration_ms[2] = { 1, 2 };
    for (int trial = 0; trial < 2; ++trial) {
        for (int tries = 0; tries < 3; ++tries) {
            switch (calibration_clock) {
                case CLOCK_HPET:
                    hpet_calibration_cycle_preamble();
                    break;
                case CLOCK_PIT:
                    pit_calibration_cycle_preamble(duration_ms[trial]);
                    break;
                default: PANIC_UNIMPLEMENTED;
            }

            // Use CPUID to serialize the instruction stream
            uint32_t _ignored;
            cpuid(0, &_ignored, &_ignored, &_ignored, &_ignored);
            uint64_t start = rdtsc();
            cpuid(0, &_ignored, &_ignored, &_ignored, &_ignored);

            switch (calibration_clock) {
                case CLOCK_HPET:
                    hpet_calibration_cycle(duration_ms[trial]);
                    break;
                case CLOCK_PIT:
                    pit_calibration_cycle(duration_ms[trial]);
                    break;
                default: PANIC_UNIMPLEMENTED;
            }

            cpuid(0, &_ignored, &_ignored, &_ignored, &_ignored);
            uint64_t end = rdtsc();
            cpuid(0, &_ignored, &_ignored, &_ignored, &_ignored);

            uint64_t tsc_ticks = end - start;
            if (tsc_ticks < best_time[trial]) {
                best_time[trial] = tsc_ticks;
            }
            LTRACEF("Calibration trial %d found %" PRIu64 " ticks/ms\n",
                    tries, tsc_ticks);
            switch (calibration_clock) {
                case CLOCK_HPET:
                    hpet_calibration_cycle_cleanup();
                    break;
                case CLOCK_PIT:
                    pit_calibration_cycle_cleanup();
                    break;
                default: PANIC_UNIMPLEMENTED;
            }
        }
    }

    tsc_ticks_per_ms = (best_time[1] - best_time[0]) / (duration_ms[1] - duration_ms[0]);

    LTRACEF("TSC calibrated: %" PRIu64 " ticks/ms\n", tsc_ticks_per_ms);

    fp_32_64_div_32_32(&ns_per_tsc, 1000 * 1000 * 1000, tsc_ticks_per_ms * 1000);
    LTRACEF("ns_per_tsc: %08x.%08x%08x\n", ns_per_tsc.l0, ns_per_tsc.l32, ns_per_tsc.l64);
}

static void platform_init_timer(uint level)
{
    const struct x86_model_info *cpu_model = x86_get_model();

    constant_tsc = false;
    if (x86_vendor == X86_VENDOR_INTEL) {
        /* This condition taken from Intel 3B 17.15 (Time-Stamp Counter).  This
         * is the negation of the non-Constant TSC section, since the Constant
         * TSC section is incomplete (the behavior is architectural going
         * forward, and modern CPUs are not on the list). */
        constant_tsc = !((cpu_model->family == 0x6 && cpu_model->model == 0x9) ||
                         (cpu_model->family == 0x6 && cpu_model->model == 0xd) ||
                         (cpu_model->family == 0xf && cpu_model->model < 0x3));
    }

    invariant_tsc = x86_feature_test(X86_FEATURE_INVAR_TSC);
    bool has_hpet = hpet_is_present();

    if (has_hpet) {
        calibration_clock = CLOCK_HPET;
        fp_32_64_div_32_32(&ns_per_hpet, 1000 * 1000 * 1000, hpet_ticks_per_ms() * 1000);
    } else {
        calibration_clock = CLOCK_PIT;
    }

    use_tsc_deadline = invariant_tsc &&
            x86_feature_test(X86_FEATURE_TSC_DEADLINE);
    if (!use_tsc_deadline) {
        calibrate_apic_timer();
    }

    const char *force_wallclock = cmdline_get("timer.wallclock");

    if (invariant_tsc && (!force_wallclock || !strcmp(force_wallclock, "tsc"))) {
        calibrate_tsc();

        // Program PIT in the software strobe configuration, but do not load
        // the count.  This will pause the PIT.
        outp(I8253_CONTROL_REG, 0x38);
        wall_clock = CLOCK_TSC;
    } else {
        if (constant_tsc) {
            // Calibrate the TSC even though it's not as good as we want, so we
            // can still let folks still use it for cheap timing.
            calibrate_tsc();
        }

        if (has_hpet && (!force_wallclock || !strcmp(force_wallclock, "hpet"))) {
            wall_clock = CLOCK_HPET;
            hpet_set_value(0);
            hpet_enable();
        } else {
            if (force_wallclock && strcmp(force_wallclock, "pit")) {
                panic("Could not satisfy timer.wallclock choice\n");
            }

            wall_clock = CLOCK_PIT;

            timer_current_time = 0;
            set_pit_frequency(1000); // ~1ms granularity

            uint32_t irq = apic_io_isa_to_global(ISA_IRQ_PIT);
            register_int_handler(irq, &pit_timer_tick, NULL);
            unmask_interrupt(irq);
        }
    }

    TRACEF("timer features: constant_tsc %d invariant_tsc %d tsc_deadline %d\n",
            constant_tsc, invariant_tsc, use_tsc_deadline);
    TRACEF("Using %s as wallclock\n", clock_name[wall_clock]);
}
LK_INIT_HOOK(timer, &platform_init_timer, LK_INIT_LEVEL_VM + 3);

status_t platform_set_oneshot_timer(platform_timer_callback callback,
                                    void *arg, lk_time_t interval)
{
    DEBUG_ASSERT(arch_ints_disabled());
    uint cpu = arch_curr_cpu_num();

    t_callback[cpu] = callback;
    callback_arg[cpu] = arg;

    if (interval > MAX_TIMER_INTERVAL)
        interval = MAX_TIMER_INTERVAL;
    if (interval < 1) interval = 1;

    if (use_tsc_deadline) {
        if (UINT64_MAX / interval < tsc_ticks_per_ms) {
            return ERR_INVALID_ARGS;
        }
        uint64_t tsc_interval = interval * tsc_ticks_per_ms;
        uint64_t deadline = rdtsc() + tsc_interval;
        LTRACEF("Scheduling oneshot timer: %" PRIu64 " deadline\n", deadline);
        apic_timer_set_tsc_deadline(deadline, false /* unmasked */);
        return NO_ERROR;
    }

    uint8_t extra_divisor = 1;
    while (apic_ticks_per_ms > UINT32_MAX / interval / extra_divisor) {
        extra_divisor *= 2;
    }
    uint32_t count = (apic_ticks_per_ms / extra_divisor) * interval;
    uint32_t divisor = apic_divisor * extra_divisor;
    ASSERT(divisor <= UINT8_MAX);
    LTRACEF("Scheduling oneshot timer: %u count, %u div\n", count, divisor);
    return apic_timer_set_oneshot(count, divisor, false /* unmasked */);
}

void platform_stop_timer(void)
{
    /* Enable interrupt mode that will stop the decreasing counter of the PIT */
    //outp(I8253_CONTROL_REG, 0x30);
    apic_timer_stop();
}

status_t platform_configure_watchdog(uint32_t frequency) {
    switch (wall_clock) {
        case CLOCK_TSC: {
            /* Use the PIT IRQ number since the PIT isn't running */
            uint32_t irq = apic_io_isa_to_global(ISA_IRQ_PIT);
            if (hpet_timer_configure_irq(0, irq) == NO_ERROR) {
                apic_io_configure_isa_irq(
                        ISA_IRQ_PIT,
                        DELIVERY_MODE_NMI,
                        IO_APIC_IRQ_UNMASK,
                        DST_MODE_PHYSICAL,
                        0,
                        0);

                uint64_t hpet_rate_ms = hpet_ticks_per_ms();
                hpet_disable();
                __UNUSED status_t status = hpet_set_value(0);
                DEBUG_ASSERT(status == NO_ERROR);
                status = hpet_timer_set_periodic(0, hpet_rate_ms * frequency / 1000);
                DEBUG_ASSERT(status == NO_ERROR);
                hpet_enable();

                return NO_ERROR;
            }
            /* Fallthrough and use the PIT instead */
        }
        case CLOCK_HPET: {
            /* Use the PIT instead of a separate HPET timer for this since
             * once the HPET is going, you can't reasonably configure a
             * periodic timer without stopping it. */
            set_pit_frequency(frequency);

            apic_io_configure_isa_irq(
                    ISA_IRQ_PIT,
                    DELIVERY_MODE_NMI,
                    IO_APIC_IRQ_UNMASK,
                    DST_MODE_PHYSICAL,
                    0,
                    0);
            printf("CONFIGURED WATCHDOG\n");
            return NO_ERROR;
        }
        default: return ERR_NOT_SUPPORTED;
    }
}
