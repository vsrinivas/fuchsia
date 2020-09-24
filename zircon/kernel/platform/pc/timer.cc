// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <debug.h>
#include <err.h>
#include <inttypes.h>
#include <lib/affine/transform.h>
#include <lib/cmdline.h>
#include <lib/counters.h>
#include <lib/fixed_point.h>
#include <platform.h>
#include <pow2.h>
#include <sys/types.h>
#include <trace.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <arch/x86/feature.h>
#include <arch/x86/pv.h>
#include <arch/x86/timer_freq.h>
#include <dev/interrupt.h>
#include <fbl/algorithm.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <ktl/iterator.h>
#include <ktl/limits.h>
#include <lk/init.h>
#include <platform/console.h>
#include <platform/pc.h>
#include <platform/pc/hpet.h>
#include <platform/pc/timer.h>
#include <platform/timer.h>

#include "platform_p.h"

extern "C" {

// Samples taken at the first instruction in the kernel.
uint64_t kernel_entry_ticks;
// ... and at the entry to normal virtual-space kernel code.
uint64_t kernel_virtual_entry_ticks;

}  // extern "C"

// That value is published as a kcounter.
KCOUNTER(timeline_zbi_entry, "boot.timeline.zbi")
KCOUNTER(timeline_virtual_entry, "boot.timeline.virtual")

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
  // Used before wall_clock is selected. current_ticks() returns 0.
  CLOCK_UNSELECTED = 0,

  CLOCK_TSC,
  CLOCK_PIT,
  CLOCK_HPET,

  CLOCK_COUNT
};

const char* clock_name[] = {
    [CLOCK_UNSELECTED] = "UNSELECTED",
    [CLOCK_TSC] = "TSC",
    [CLOCK_PIT] = "PIT",
    [CLOCK_HPET] = "HPET",
};
static_assert(ktl::size(clock_name) == CLOCK_COUNT, "");

// PIT time accounting info
static struct fp_32_64 us_per_pit;
static volatile uint64_t pit_ticks;
static uint16_t pit_divisor;
static uint32_t ns_per_pit_rounded_up;

// Whether or not we have an Invariant TSC (controls whether we use the PIT or
// not after initialization).  The Invariant TSC is rate-invariant under P-, C-,
// and T-state transitions.
static bool invariant_tsc;
// Whether or not we have a Constant TSC (controls whether we bother calibrating
// the TSC).  Constant TSC predates the Invariant TSC.  The Constant TSC is
// rate-invariant under P-state transitions.
static bool constant_tsc;

static enum clock_source wall_clock = CLOCK_UNSELECTED;
static enum clock_source calibration_clock;

// APIC timer calibration values
static bool use_tsc_deadline;
static uint32_t apic_ticks_per_ms = 0;
static struct fp_32_64 apic_ticks_per_ns;
static uint8_t apic_divisor = 0;

// TSC timer calibration values
static uint64_t tsc_ticks_per_ms;
static struct fp_32_64 ns_per_tsc;
static struct fp_32_64 tsc_per_ns;
static uint32_t ns_per_tsc_rounded_up;
static affine::Ratio rdtsc_ticks_to_clock_monotonic;

// HPET calibration values
static struct fp_32_64 ns_per_hpet;
static uint32_t ns_per_hpet_rounded_up;
affine::Ratio hpet_ticks_to_clock_monotonic;  // Non-static so that hpet_init has access

#define INTERNAL_FREQ 1193182U
#define INTERNAL_FREQ_3X 3579546U

#define INTERNAL_FREQ_TICKS_PER_MS (INTERNAL_FREQ / 1000)

/* Maximum amount of time that can be program on the timer to schedule the next
 *  interrupt, in miliseconds */
#define MAX_TIMER_INTERVAL ZX_MSEC(55)

#define LOCAL_TRACE 0

zx_ticks_t current_ticks_rdtsc(void) { return _rdtsc(); }

zx_ticks_t current_ticks_hpet(void) { return hpet_get_value(); }

zx_ticks_t current_ticks_pit(void) { return pit_ticks; }

zx_ticks_t platform_current_ticks() {
  // Directly call the ticks functions to avoid the cost of a virtual (indirect) call.
  if (wall_clock == CLOCK_TSC) {
    return current_ticks_rdtsc();
  } else {
    switch (wall_clock) {
      case CLOCK_UNSELECTED:
        return 0;
      case CLOCK_PIT:
        return current_ticks_pit();
      case CLOCK_HPET:
        return current_ticks_hpet();
      default:
        PANIC_UNIMPLEMENTED;
    }
  }
}

const affine::Ratio& rdtsc_to_nanos() { return rdtsc_ticks_to_clock_monotonic; }

// Round up t to a clock tick, so that when the APIC timer fires, the wall time
// will have elapsed.
static zx_time_t discrete_time_roundup(zx_time_t t) {
  zx_duration_t value;
  switch (wall_clock) {
    case CLOCK_TSC: {
      value = ns_per_tsc_rounded_up;
      break;
    }
    case CLOCK_HPET: {
      value = ns_per_hpet_rounded_up;
      break;
    }
    case CLOCK_PIT: {
      value = ns_per_pit_rounded_up;
      break;
    }
    default:
      panic("Invalid wall clock source\n");
  }

  return zx_time_add_duration(t, value);
}

// The PIT timer will keep track of wall time if we aren't using the TSC
static interrupt_eoi pit_timer_tick(void* arg) {
  pit_ticks += 1;
  return IRQ_EOI_DEACTIVATE;
}

// The APIC timers will call this when they fire
void platform_handle_apic_timer_tick(void) { timer_tick(current_time()); }

static void set_pit_frequency(uint32_t frequency) {
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
  fp_32_64_div_32_32(&us_per_pit, 1000 * 1000 * 3 * count, INTERNAL_FREQ_3X);

  // Add 1us to the PIT tick rate to deal with rounding
  ns_per_pit_rounded_up = (u32_mul_u64_fp32_64(1, us_per_pit) + 1) * 1000;

  // dprintf(DEBUG, "set_pit_frequency: pit_divisor=%04x\n", pit_divisor);

  /*
   * setup the Programmable Interval Timer
   * timer 0, mode 2, binary counter, LSB followed by MSB
   */
  outp(I8253_CONTROL_REG, 0x34);
  outp(I8253_DATA_REG, static_cast<uint8_t>(pit_divisor));       // LSB
  outp(I8253_DATA_REG, static_cast<uint8_t>(pit_divisor >> 8));  // MSB
}

static inline void pit_calibration_cycle_preamble(uint16_t ms) {
  // Make the PIT run for
  const uint16_t init_pic_count = static_cast<uint16_t>(INTERNAL_FREQ_TICKS_PER_MS * ms);
  // Program PIT in the interrupt on terminal count configuration,
  // this makes it count down and set the output high when it hits 0.
  outp(I8253_CONTROL_REG, 0x30);
  outp(I8253_DATA_REG, static_cast<uint8_t>(init_pic_count));  // LSB
}

static inline void pit_calibration_cycle(uint16_t ms) {
  // Make the PIT run for ms millis, see comments in the preamble
  const uint16_t init_pic_count = static_cast<uint16_t>(INTERNAL_FREQ_TICKS_PER_MS * ms);
  outp(I8253_DATA_REG, static_cast<uint8_t>(init_pic_count >> 8));  // MSB

  uint8_t status = 0;
  do {
    // Send a read-back command that latches the status of ch0
    outp(I8253_CONTROL_REG, 0xe2);
    status = inp(I8253_DATA_REG);
    // Wait for bit 7 (output) to go high and for bit 6 (null count) to go low
  } while ((status & 0xc0) != 0x80);
}

static inline void pit_calibration_cycle_cleanup(void) {
  // Stop the PIT by starting a mode change but not writing a counter
  outp(I8253_CONTROL_REG, 0x38);
}

static inline void hpet_calibration_cycle_preamble(void) { hpet_enable(); }

static inline void hpet_calibration_cycle(uint16_t ms) { hpet_wait_ms(ms); }

static inline void hpet_calibration_cycle_cleanup(void) { hpet_disable(); }

static void calibrate_apic_timer(void) {
  ASSERT(arch_ints_disabled());

  const uint64_t apic_freq = x86_lookup_core_crystal_freq();
  if (apic_freq != 0) {
    ASSERT(apic_freq / 1000 <= UINT32_MAX);
    apic_ticks_per_ms = static_cast<uint32_t>(apic_freq / 1000);
    apic_divisor = 1;
    fp_32_64_div_32_32(&apic_ticks_per_ns, apic_ticks_per_ms, 1000 * 1000);
    printf("APIC frequency: %" PRIu32 " ticks/ms\n", apic_ticks_per_ms);
    return;
  }

  printf("Could not find APIC frequency: Calibrating APIC with %s\n",
         clock_name[calibration_clock]);

  apic_divisor = 1;
outer:
  while (apic_divisor != 0) {
    uint32_t best_time[2] = {UINT32_MAX, UINT32_MAX};
    const uint16_t duration_ms[2] = {2, 4};
    for (int trial = 0; trial < 2; ++trial) {
      for (int tries = 0; tries < 3; ++tries) {
        switch (calibration_clock) {
          case CLOCK_HPET:
            hpet_calibration_cycle_preamble();
            break;
          case CLOCK_PIT:
            pit_calibration_cycle_preamble(duration_ms[trial]);
            break;
          default:
            PANIC_UNIMPLEMENTED;
        }

        // Setup APIC timer to count down with interrupt masked
        zx_status_t status = apic_timer_set_oneshot(UINT32_MAX, apic_divisor, true);
        ASSERT(status == ZX_OK);

        switch (calibration_clock) {
          case CLOCK_HPET:
            hpet_calibration_cycle(duration_ms[trial]);
            break;
          case CLOCK_PIT:
            pit_calibration_cycle(duration_ms[trial]);
            break;
          default:
            PANIC_UNIMPLEMENTED;
        }

        uint32_t apic_ticks = UINT32_MAX - apic_timer_current_count();
        if (apic_ticks < best_time[trial]) {
          best_time[trial] = apic_ticks;
        }
        LTRACEF("Calibration trial %d found %u ticks/ms\n", tries, apic_ticks);

        switch (calibration_clock) {
          case CLOCK_HPET:
            hpet_calibration_cycle_cleanup();
            break;
          case CLOCK_PIT:
            pit_calibration_cycle_cleanup();
            break;
          default:
            PANIC_UNIMPLEMENTED;
        }
      }

      // If the APIC ran out of time every time, try again with a higher
      // divisor
      if (best_time[trial] == UINT32_MAX) {
        apic_divisor = static_cast<uint8_t>(apic_divisor * 2);
        goto outer;
      }
    }
    apic_ticks_per_ms = (best_time[1] - best_time[0]) / (duration_ms[1] - duration_ms[0]);
    fp_32_64_div_32_32(&apic_ticks_per_ns, apic_ticks_per_ms, 1000 * 1000);
    break;
  }
  ASSERT(apic_divisor != 0);

  printf("APIC timer calibrated: %" PRIu32 " ticks/ms, divisor %d\n", apic_ticks_per_ms,
         apic_divisor);
}

static inline void serialize_instructions(void) {
  // Executing a cpuid instruction serializes the instruction stream. Use inline
  // assembly to force the compiler to include it, unlike the builtins.
  __asm__ __volatile__("cpuid" : : "rax"(0) : "rax", "rbx", "rcx", "rdx", "memory");
}

static uint64_t calibrate_tsc_count(uint16_t duration_ms) {
  zx_ticks_t best_time = ktl::numeric_limits<zx_ticks_t>::max();

  for (int tries = 0; tries < 3; ++tries) {
    switch (calibration_clock) {
      case CLOCK_HPET:
        hpet_calibration_cycle_preamble();
        break;
      case CLOCK_PIT:
        pit_calibration_cycle_preamble(duration_ms);
        break;
      default:
        PANIC_UNIMPLEMENTED;
    }

    serialize_instructions();
    uint64_t start = _rdtsc();
    serialize_instructions();

    switch (calibration_clock) {
      case CLOCK_HPET:
        hpet_calibration_cycle(duration_ms);
        break;
      case CLOCK_PIT:
        pit_calibration_cycle(duration_ms);
        break;
      default:
        PANIC_UNIMPLEMENTED;
    }

    serialize_instructions();
    zx_ticks_t end = _rdtsc();
    serialize_instructions();

    zx_ticks_t tsc_ticks = end - start;
    if (tsc_ticks < best_time) {
      best_time = tsc_ticks;
    }
    LTRACEF("Calibration trial %d found %" PRId64 " ticks/ms\n", tries, tsc_ticks);
    switch (calibration_clock) {
      case CLOCK_HPET:
        hpet_calibration_cycle_cleanup();
        break;
      case CLOCK_PIT:
        pit_calibration_cycle_cleanup();
        break;
      default:
        PANIC_UNIMPLEMENTED;
    }
  }

  return best_time;
}

static void calibrate_tsc(bool has_pv_clock) {
  ASSERT(arch_ints_disabled());

  const uint64_t tsc_freq = has_pv_clock ? pv_clock_get_tsc_freq() : x86_lookup_tsc_freq();
  if (tsc_freq != 0) {
    uint64_t N = 1'000'000'000;
    uint64_t D = tsc_freq;
    affine::Ratio::Reduce(&N, &D);

    // ASSERT that we can represent this as a 32 bit ratio.  If we cannot,
    // it means that tsc_freq is a number so large, and with so few prime
    // factors of 2 and 5, that it cannot be reduced to fit into a 32 bit
    // integer.  This is pretty unreasonable, for now, just assert that it
    // will not happen.
    ZX_ASSERT_MSG(
        (N <= ktl::numeric_limits<uint32_t>::max()) && (D <= ktl::numeric_limits<uint32_t>::max()),
        "Clock monotonic ticks : RDTSC ticks ratio (%lu : %lu) "
        "too large to store in a 32 bit ratio!!",
        N, D);
    rdtsc_ticks_to_clock_monotonic = {static_cast<uint32_t>(N), static_cast<uint32_t>(D)};

    tsc_ticks_per_ms = tsc_freq / 1000;
    printf("TSC frequency: %" PRIu64 " ticks/ms\n", tsc_ticks_per_ms);
  } else {
    printf("Could not find TSC frequency: Calibrating TSC with %s\n",
           clock_name[calibration_clock]);

    uint32_t duration_ms[2] = {2, 4};
    uint64_t best_time[2] = {calibrate_tsc_count(static_cast<uint16_t>(duration_ms[0])),
                             calibrate_tsc_count(static_cast<uint16_t>(duration_ms[1]))};

    while (best_time[0] >= best_time[1] && 2 * duration_ms[1] < MAX_TIMER_INTERVAL) {
      duration_ms[0] = duration_ms[1];
      duration_ms[1] *= 2;
      best_time[0] = best_time[1];
      best_time[1] = calibrate_tsc_count(static_cast<uint16_t>(duration_ms[1]));
    }

    ASSERT(best_time[0] < best_time[1]);

    uint64_t tsc_ticks_per_sec =
        ((best_time[1] - best_time[0]) * 1000) / (duration_ms[1] - duration_ms[0]);

    ZX_ASSERT_MSG(tsc_ticks_per_sec <= ktl::numeric_limits<uint32_t>::max(),
                  "Estimated TSC (%lu) is to high!\n", tsc_ticks_per_sec);

    tsc_ticks_per_ms = tsc_ticks_per_sec / 1000;
    rdtsc_ticks_to_clock_monotonic = {1'000'000'000, static_cast<uint32_t>(tsc_ticks_per_sec)};

    printf("TSC calibrated: %" PRIu64 " ticks/ms\n", tsc_ticks_per_ms);
  }

  ASSERT(tsc_ticks_per_ms <= UINT32_MAX);
  fp_32_64_div_32_32(&ns_per_tsc, 1000 * 1000, static_cast<uint32_t>(tsc_ticks_per_ms));
  fp_32_64_div_32_32(&tsc_per_ns, static_cast<uint32_t>(tsc_ticks_per_ms), 1000 * 1000);
  // Add 1ns to conservatively deal with rounding
  ns_per_tsc_rounded_up = u32_mul_u64_fp32_64(1, ns_per_tsc) + 1;

  LTRACEF("ns_per_tsc: %08x.%08x%08x\n", ns_per_tsc.l0, ns_per_tsc.l32, ns_per_tsc.l64);
}

static uint64_t hpet_ticks_per_ms(void) {
  extern uint64_t _hpet_ticks_per_ms;
  return _hpet_ticks_per_ms;
}

static void pc_init_timer(uint level) {
  const struct x86_model_info* cpu_model = x86_get_model();

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

  bool has_pv_clock = x86_hypervisor_has_pv_clock();
  if (has_pv_clock) {
    zx_status_t status = pv_clock_init();
    if (status == ZX_OK) {
      invariant_tsc = pv_clock_is_stable();
    } else {
      has_pv_clock = false;
    }
  }

  bool has_hpet = hpet_is_present();
  if (has_hpet) {
    calibration_clock = CLOCK_HPET;
    const uint64_t hpet_ms_rate = hpet_ticks_per_ms();
    ASSERT(hpet_ms_rate <= UINT32_MAX);
    printf("HPET frequency: %" PRIu64 " ticks/ms\n", hpet_ms_rate);
    fp_32_64_div_32_32(&ns_per_hpet, 1000 * 1000, static_cast<uint32_t>(hpet_ms_rate));
    // Add 1ns to conservatively deal with rounding
    ns_per_hpet_rounded_up = u32_mul_u64_fp32_64(1, ns_per_hpet) + 1;
  } else {
    calibration_clock = CLOCK_PIT;
  }

  const char* force_wallclock = gCmdline.GetString("kernel.wallclock");
  bool use_invariant_tsc = invariant_tsc && (!force_wallclock || !strcmp(force_wallclock, "tsc"));

  use_tsc_deadline = use_invariant_tsc && x86_feature_test(X86_FEATURE_TSC_DEADLINE);
  if (!use_tsc_deadline) {
    calibrate_apic_timer();
  }

  if (use_invariant_tsc) {
    calibrate_tsc(has_pv_clock);

    // Program PIT in the software strobe configuration, but do not load
    // the count.  This will pause the PIT.
    outp(I8253_CONTROL_REG, 0x38);

    // Set up our wall clock to rdtsc, and stash the initial
    // transformation from ticks to clock monotonic.
    platform_set_ticks_to_time_ratio(rdtsc_ticks_to_clock_monotonic);
    wall_clock = CLOCK_TSC;

    // If not using TSC, these samples won't be available since they'd
    // have to be converted from TSC ticks to prevailing ticks.
    timeline_zbi_entry.Set(kernel_entry_ticks);
    timeline_virtual_entry.Set(kernel_virtual_entry_ticks);
  } else {
    if (constant_tsc || invariant_tsc) {
      // Calibrate the TSC even though it's not as good as we want, so we
      // can still let folks still use it for cheap timing.
      calibrate_tsc(has_pv_clock);
    }

    if (has_hpet && (!force_wallclock || !strcmp(force_wallclock, "hpet"))) {
      // Set up our wall clock to the HPET, and stash the initial
      // transformation from ticks to clock monotonic.
      platform_set_ticks_to_time_ratio(hpet_ticks_to_clock_monotonic);
      wall_clock = CLOCK_HPET;
      hpet_set_value(0);
      hpet_enable();
    } else {
      if (force_wallclock && strcmp(force_wallclock, "pit")) {
        panic("Could not satisfy kernel.wallclock choice\n");
      }

      // Set up our wall clock to pit, and stash the initial
      // transformation from ticks to clock monotonic.
      platform_set_ticks_to_time_ratio({1'000'000, 1});
      wall_clock = CLOCK_PIT;

      set_pit_frequency(1000);  // ~1ms granularity

      uint32_t irq = apic_io_isa_to_global(ISA_IRQ_PIT);
      zx_status_t status = register_permanent_int_handler(irq, &pit_timer_tick, NULL);
      DEBUG_ASSERT(status == ZX_OK);
      unmask_interrupt(irq);
    }
  }

  printf("timer features: constant_tsc %d invariant_tsc %d tsc_deadline %d\n", constant_tsc,
         invariant_tsc, use_tsc_deadline);
  printf("Using %s as wallclock\n", clock_name[wall_clock]);
}
LK_INIT_HOOK(timer, &pc_init_timer, LK_INIT_LEVEL_VM + 3)

zx_status_t platform_set_oneshot_timer(zx_time_t deadline) {
  DEBUG_ASSERT(arch_ints_disabled());

  if (deadline < 0) {
    deadline = 0;
  }
  deadline = discrete_time_roundup(deadline);
  DEBUG_ASSERT(deadline > 0);

  if (use_tsc_deadline) {
    // Check if the deadline would overflow the TSC.
    const uint64_t tsc_ticks_per_ns = tsc_ticks_per_ms / ZX_MSEC(1);
    if (UINT64_MAX / deadline < tsc_ticks_per_ns) {
      return ZX_ERR_INVALID_ARGS;
    }

    // We rounded up to the tick after above.
    const uint64_t tsc_deadline = u64_mul_u64_fp32_64(deadline, tsc_per_ns);
    LTRACEF("Scheduling oneshot timer: %" PRIu64 " deadline\n", tsc_deadline);
    apic_timer_set_tsc_deadline(tsc_deadline, false /* unmasked */);
    return ZX_OK;
  }

  const zx_time_t now = current_time();
  if (now >= deadline) {
    // Deadline has already passed. We still need to schedule a timer so that
    // the interrupt fires.
    LTRACEF("Scheduling oneshot timer for min duration\n");
    return apic_timer_set_oneshot(1, 1, false /* unmasked */);
  }
  const zx_duration_t interval = zx_time_sub_time(deadline, now);
  DEBUG_ASSERT(interval > 0);

  uint64_t apic_ticks_needed = u64_mul_u64_fp32_64(interval, apic_ticks_per_ns);
  if (apic_ticks_needed == 0) {
    apic_ticks_needed = 1;
  }

  // Find the shift needed for this timeout, since count is 32-bit.
  const uint highest_set_bit = log2_ulong_floor(apic_ticks_needed);
  uint8_t extra_shift = (highest_set_bit <= 31) ? 0 : static_cast<uint8_t>(highest_set_bit - 31);
  if (extra_shift > 8) {
    extra_shift = 8;
  }

  uint32_t divisor = apic_divisor << extra_shift;
  uint32_t count;
  // If the divisor is too large, we're at our maximum timeout.  Saturate the
  // timer.  It'll fire earlier than requested, but the scheduler will notice
  // and ask us to set the timer up again.
  if (divisor <= 128) {
    count = (uint32_t)(apic_ticks_needed >> extra_shift);
    DEBUG_ASSERT((apic_ticks_needed >> extra_shift) <= UINT32_MAX);
  } else {
    divisor = 128;
    count = UINT32_MAX;
  }

  // Make sure we're not underflowing
  if (count == 0) {
    DEBUG_ASSERT(divisor == 1);
    count = 1;
  }

  LTRACEF("Scheduling oneshot timer: %u count, %u div\n", count, divisor);
  return apic_timer_set_oneshot(count, static_cast<uint8_t>(divisor), false /* unmasked */);
}

void platform_stop_timer(void) {
  /* Enable interrupt mode that will stop the decreasing counter of the PIT */
  // outp(I8253_CONTROL_REG, 0x30);
  apic_timer_stop();
}

void platform_shutdown_timer(void) {
  DEBUG_ASSERT(arch_ints_disabled());

  if (x86_hypervisor_has_pv_clock() && arch_curr_cpu_num() == 0) {
    pv_clock_shutdown();
  }
}

// Currently, usermode can access our source of ticks only if we have chosen TSC
// to be our tick counter.  Otherwise, they will need to go through a syscall.
//
// In theory, we can fix this, but it would require having the vDSO map some
// read-only memory in the user mode process (either the HPET registers, or the
// variable which represents the PIT timer).  Currently, doing this is not
// something we support, and the vast majority of x64 systems that we run on
// have an invariant TSC which is accessible from usermode.  For now, we just
// take the syscall hit instead of attempting to get more fancy.
bool platform_usermode_can_access_tick_registers(void) { return (wall_clock == CLOCK_TSC); }

static uint64_t saved_hpet_val;
void pc_prep_suspend_timer(void) {
  if (hpet_is_present()) {
    saved_hpet_val = hpet_get_value();
  }
}

void pc_resume_timer(void) {
  switch (wall_clock) {
    case CLOCK_HPET:
      hpet_set_value(saved_hpet_val);
      hpet_enable();
      break;
    case CLOCK_PIT: {
      set_pit_frequency(1000);  // ~1ms granularity

      uint32_t irq = apic_io_isa_to_global(ISA_IRQ_PIT);
      unmask_interrupt(irq);
      break;
    }
    default:
      break;
  }
}
