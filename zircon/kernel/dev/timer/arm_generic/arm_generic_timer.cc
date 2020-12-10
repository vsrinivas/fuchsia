// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2013, Google Inc. All rights reserved.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <inttypes.h>
#include <lib/affine/ratio.h>
#include <lib/arch/intrin.h>
#include <lib/cmdline.h>
#include <lib/counters.h>
#include <lib/fixed_point.h>
#include <lib/unittest/unittest.h>
#include <platform.h>
#include <pow2.h>
#include <trace.h>
#include <zircon/boot/driver-config.h>
#include <zircon/types.h>

#include <arch/quirks.h>
#include <dev/interrupt.h>
#include <dev/timer/arm_generic.h>
#include <ktl/atomic.h>
#include <ktl/limits.h>
#include <lk/init.h>
#include <pdev/driver.h>
#include <platform/timer.h>

#define LOCAL_TRACE 0

// AArch64 timer control registers
#define TIMER_REG_CNTKCTL "cntkctl_el1"
#define TIMER_REG_CNTFRQ "cntfrq_el0"

// CNTP AArch64 registers
#define TIMER_REG_CNTP_CTL "cntp_ctl_el0"
#define TIMER_REG_CNTP_CVAL "cntp_cval_el0"
#define TIMER_REG_CNTP_TVAL "cntp_tval_el0"
#define TIMER_REG_CNTPCT "cntpct_el0"

// CNTPS "AArch64" registers
#define TIMER_REG_CNTPS_CTL "cntps_ctl_el1"
#define TIMER_REG_CNTPS_CVAL "cntps_cval_el1"
#define TIMER_REG_CNTPS_TVAL "cntps_tval_el1"

// CNTV "AArch64" registers
#define TIMER_REG_CNTV_CTL "cntv_ctl_el0"
#define TIMER_REG_CNTV_CVAL "cntv_cval_el0"
#define TIMER_REG_CNTV_TVAL "cntv_tval_el0"
#define TIMER_REG_CNTVCT "cntvct_el0"

extern "C" {

// Samples taken at the first instruction in the kernel.
uint64_t kernel_entry_ticks[2];  // cntpct, cntvct
// ... and at the entry to normal virtual-space kernel code.
uint64_t kernel_virtual_entry_ticks[2];  // cntpct, cntvct

}  // extern "C"

// That value is published as a kcounter.
KCOUNTER(timeline_zbi_entry, "boot.timeline.zbi")
KCOUNTER(timeline_virtual_entry, "boot.timeline.virtual")

namespace {

// Global saved config state
int timer_irq;
uint32_t timer_cntfrq; // Timer tick rate in Hz.

enum timer_irq_assignment {
  IRQ_PHYS,
  IRQ_VIRT,
  IRQ_SPHYS,
};

timer_irq_assignment timer_assignment;

}  // anonymous namespace

zx_time_t cntpct_to_zx_time(uint64_t cntpct) {
  DEBUG_ASSERT(cntpct < static_cast<uint64_t>(ktl::numeric_limits<int64_t>::max()));
  return platform_get_ticks_to_time_ratio().Scale(static_cast<int64_t>(cntpct));
}

static uint32_t read_cntp_ctl() { return __arm_rsr(TIMER_REG_CNTP_CTL); }

static uint32_t read_cntv_ctl() { return __arm_rsr(TIMER_REG_CNTV_CTL); }

static uint32_t read_cntps_ctl() { return __arm_rsr(TIMER_REG_CNTPS_CTL); }

static void write_cntp_ctl(uint32_t val) {
  LTRACEF_LEVEL(3, "cntp_ctl: 0x%x %x\n", val, read_cntp_ctl());
  __arm_wsr(TIMER_REG_CNTP_CTL, val);
  __isb(ARM_MB_SY);
}

static void write_cntv_ctl(uint32_t val) {
  LTRACEF_LEVEL(3, "cntv_ctl: 0x%x %x\n", val, read_cntv_ctl());
  __arm_wsr(TIMER_REG_CNTV_CTL, val);
  __isb(ARM_MB_SY);
}

static void write_cntps_ctl(uint32_t val) {
  LTRACEF_LEVEL(3, "cntps_ctl: 0x%x %x\n", val, read_cntps_ctl());
  __arm_wsr(TIMER_REG_CNTPS_CTL, val);
  __isb(ARM_MB_SY);
}

static void write_cntp_cval(uint64_t val) {
  LTRACEF_LEVEL(3, "cntp_cval: 0x%016" PRIx64 ", %" PRIu64 "\n", val, val);
  __arm_wsr64(TIMER_REG_CNTP_CVAL, val);
  __isb(ARM_MB_SY);
}

static void write_cntv_cval(uint64_t val) {
  LTRACEF_LEVEL(3, "cntv_cval: 0x%016" PRIx64 ", %" PRIu64 "\n", val, val);
  __arm_wsr64(TIMER_REG_CNTV_CVAL, val);
  __isb(ARM_MB_SY);
}

static void write_cntps_cval(uint64_t val) {
  LTRACEF_LEVEL(3, "cntps_cval: 0x%016" PRIx64 ", %" PRIu64 "\n", val, val);
  __arm_wsr64(TIMER_REG_CNTPS_CVAL, val);
  __isb(ARM_MB_SY);
}

static void write_cntp_tval(int32_t val) {
  LTRACEF_LEVEL(3, "cntp_tval: %d\n", val);
  __arm_wsr(TIMER_REG_CNTP_TVAL, val);
  __isb(ARM_MB_SY);
}

static void write_cntv_tval(int32_t val) {
  LTRACEF_LEVEL(3, "cntv_tval: %d\n", val);
  __arm_wsr(TIMER_REG_CNTV_TVAL, val);
  __isb(ARM_MB_SY);
}

static void write_cntps_tval(int32_t val) {
  LTRACEF_LEVEL(3, "cntps_tval: %d\n", val);
  __arm_wsr(TIMER_REG_CNTPS_TVAL, val);
  __isb(ARM_MB_SY);
}

static uint64_t read_cntpct_a73() {
  // Workaround for Cortex-A73 erratum 858921.
  // Fix will be applied to all cores, as two consecutive reads should be
  // faster than checking if core is A73 and branching before every read.
  const uint64_t old_read = __arm_rsr64(TIMER_REG_CNTPCT);
  // TODO(fxbug.dev/44780): Prevent buggy compiler from CSE'ing the two samples!
  // Remove this when the compiler is fixed.
  __asm__ volatile("");
  const uint64_t new_read = __arm_rsr64(TIMER_REG_CNTPCT);

  return (((old_read ^ new_read) >> 32) & 1) ? old_read : new_read;
}

static uint64_t read_cntvct_a73() {
  // Workaround for Cortex-A73 erratum 858921.
  // Fix will be applied to all cores, as two consecutive reads should be
  // faster than checking if core is A73 and branching before every read.
  const uint64_t old_read = __arm_rsr64(TIMER_REG_CNTVCT);
  // TODO(fxbug.dev/44780): Prevent buggy compiler from CSE'ing the two samples!
  // Remove this when the compiler is fixed.
  __asm__ volatile("");
  const uint64_t new_read = __arm_rsr64(TIMER_REG_CNTVCT);

  return (((old_read ^ new_read) >> 32) & 1) ? old_read : new_read;
}

static uint64_t read_cntpct() { return __arm_rsr64(TIMER_REG_CNTPCT); }

static uint64_t read_cntvct() { return __arm_rsr64(TIMER_REG_CNTVCT); }

struct timer_reg_procs {
  void (*write_ctl)(uint32_t val);
  void (*write_cval)(uint64_t val);
  void (*write_tval)(int32_t val);
  uint64_t (*read_ct)();
};

__UNUSED static const struct timer_reg_procs cntp_procs = {
    .write_ctl = write_cntp_ctl,
    .write_cval = write_cntp_cval,
    .write_tval = write_cntp_tval,
    .read_ct = read_cntpct,
};

__UNUSED static const struct timer_reg_procs cntp_procs_a73 = {
    .write_ctl = write_cntp_ctl,
    .write_cval = write_cntp_cval,
    .write_tval = write_cntp_tval,
    .read_ct = read_cntpct_a73,
};

__UNUSED static const struct timer_reg_procs cntv_procs = {
    .write_ctl = write_cntv_ctl,
    .write_cval = write_cntv_cval,
    .write_tval = write_cntv_tval,
    .read_ct = read_cntvct,
};

__UNUSED static const struct timer_reg_procs cntv_procs_a73 = {
    .write_ctl = write_cntv_ctl,
    .write_cval = write_cntv_cval,
    .write_tval = write_cntv_tval,
    .read_ct = read_cntvct_a73,
};

__UNUSED static const struct timer_reg_procs cntps_procs = {
    .write_ctl = write_cntps_ctl,
    .write_cval = write_cntps_cval,
    .write_tval = write_cntps_tval,
    .read_ct = read_cntpct,
};

__UNUSED static const struct timer_reg_procs cntps_procs_a73 = {
    .write_ctl = write_cntps_ctl,
    .write_cval = write_cntps_cval,
    .write_tval = write_cntps_tval,
    .read_ct = read_cntpct_a73,
};

#if (TIMER_ARM_GENERIC_SELECTED_CNTV)
static const struct timer_reg_procs* reg_procs = &cntv_procs;
#else
static const struct timer_reg_procs* reg_procs = &cntp_procs;
#endif

static inline void write_ctl(uint32_t val) { reg_procs->write_ctl(val); }

static inline void write_cval(uint64_t val) { reg_procs->write_cval(val); }

static inline void write_tval(uint32_t val) { reg_procs->write_tval(val); }

static zx_ticks_t read_ct() {
  zx_ticks_t cntpct = static_cast<zx_ticks_t>(reg_procs->read_ct());
  LTRACEF_LEVEL(3, "cntpct: 0x%016" PRIx64 ", %" PRIi64 "\n", static_cast<uint64_t>(cntpct),
                cntpct);
  return cntpct;
}

static interrupt_eoi platform_tick(void* arg) {
  write_ctl(0);
  timer_tick(current_time());
  return IRQ_EOI_DEACTIVATE;
}

zx_ticks_t platform_current_ticks() { return read_ct(); }

zx_status_t platform_set_oneshot_timer(zx_time_t deadline) {
  DEBUG_ASSERT(arch_ints_disabled());

  if (deadline < 0) {
    deadline = 0;
  }

  // Add one to the deadline, since with very high probability the deadline
  // straddles a counter tick.
  const affine::Ratio time_to_ticks = platform_get_ticks_to_time_ratio().Inverse();
  const uint64_t cntpct_deadline = time_to_ticks.Scale(deadline) + 1;

  // Even if the deadline has already passed, the ARMv8-A timer will fire the
  // interrupt.
  write_cval(cntpct_deadline);
  write_ctl(1);

  return 0;
}

void platform_stop_timer() { write_ctl(0); }

void platform_shutdown_timer() {
  DEBUG_ASSERT(arch_ints_disabled());
  mask_interrupt(timer_irq);
}

bool platform_usermode_can_access_tick_registers() {
  // We always use the ARM generic timer for the tick counter, and these
  // registers are accessible from usermode
  return true;
}

template <bool AllowDebugPrint = false>
static inline affine::Ratio arm_generic_timer_compute_conversion_factors(uint32_t cntfrq) {
  affine::Ratio cntpct_to_nsec = {ZX_SEC(1), cntfrq};
  if constexpr (AllowDebugPrint) {
    dprintf(SPEW, "arm generic timer cntpct_per_nsec: %u/%u\n", cntpct_to_nsec.numerator(),
            cntpct_to_nsec.denominator());
  }
  return cntpct_to_nsec;
}

static void enable_event_stream(uint32_t cntfrq) {
  // Check to see if it's enabled in the command line
  bool enable = gCmdline.GetBool("kernel.arm64.event-stream.enable", false);
  if (!enable) {
    return;
  }

  // Default target frequency is 10khz
  uint32_t target_event_freq = gCmdline.GetUInt32("kernel.arm64.event-stream.freq-hz", 10000);

  // Compute the closest power of two from the timer frequency to get to the target.
  //
  // The mechanism to select the rate of the event counter is to select which bit in the virtual
  // counter it should watch for a transition from 0->1 or 1->0 on. This has the effect of
  // of selecting a power of two to divide the virtual counter by plus one.
  //
  // There's no real out of range value here. Everything gets clamped to a shift value of [0, 15].
  uint shift;
  for (shift = 0; shift <= 14; shift++) {
    // Find a matching shift to the target frequency within range. If the target frequency is too
    // large even for shift 0 then it'll just pick shift 0 because of the <=.
    if (log2_uint_floor(cntfrq >> (shift + 1)) <= log2_uint_floor(target_event_freq)) {
      break;
    }
  }
  // If we ran off the end of the for loop 15 is the max shift and is okay
  DEBUG_ASSERT(shift <= 15);

  uint32_t real_event_freq = (cntfrq >> (shift + 1));

  // Enable the event stream
  uint64_t cntkctl = __arm_rsr64(TIMER_REG_CNTKCTL);
  // Set the trigger bit (field 7:4)
  cntkctl &= ~(0xfUL << 4);
  cntkctl |= shift << 4;  // EVNTI
  // Clear the transition bit to 0
  cntkctl &= ~(1 << 3);  // ENVTDIR
  // Enable the stream
  cntkctl |= (1 << 2);  // EVNTEN
  __arm_wsr64(TIMER_REG_CNTKCTL, cntkctl);

  dprintf(INFO, "arm generic timer enabling event stream on cpu %u: shift %u, %u Hz\n",
          arch_curr_cpu_num(), shift, real_event_freq);
}

static void arm_generic_timer_init(uint32_t freq_override) {
  if (freq_override == 0) {
    // Read the firmware supplied cntfrq register. Note: it may not be correct
    // in buggy firmware situations, so always provide a mechanism to override it.
    timer_cntfrq = __arm_rsr(TIMER_REG_CNTFRQ);
    LTRACEF("cntfrq: %#08x, %u\n", timer_cntfrq, timer_cntfrq);
  } else {
    timer_cntfrq = freq_override;
  }

  dprintf(INFO, "arm generic timer freq %u Hz\n", timer_cntfrq);

  // No way to reasonably continue. Just hard stop.
  ASSERT(timer_cntfrq != 0);

  platform_set_ticks_to_time_ratio(
      arm_generic_timer_compute_conversion_factors<true>(timer_cntfrq));

  // Set up the hardware timer irq handler for this vector. Use the permanent irq handler
  // registraion scheme since it is enabled on all cpus and does not need any locking
  // for reentrancy and deregistration purposes.
  zx_status_t status = register_permanent_int_handler(timer_irq, &platform_tick, NULL);
  DEBUG_ASSERT(status == ZX_OK);

  // assert that access to the virtual counter is available in EL0
  auto cntkctl = __arm_rsr64(TIMER_REG_CNTKCTL);
  ASSERT(cntkctl & (1 << 1));  // EL0VCTEN

  // try to enable the event stream if requested
  enable_event_stream(timer_cntfrq);

  // enable the IRQ on the boot cpu
  LTRACEF("unmask irq %d on cpu %u\n", timer_irq, arch_curr_cpu_num());
  unmask_interrupt(timer_irq);
}

static void arm_generic_timer_init_secondary_cpu(uint level) {
  // try to enable the event stream if requested
  enable_event_stream(timer_cntfrq);

  LTRACEF("unmask irq %d on cpu %u\n", timer_irq, arch_curr_cpu_num());
  unmask_interrupt(timer_irq);
}

// secondary cpu initialize the timer just before the kernel starts with interrupts enabled
LK_INIT_HOOK_FLAGS(arm_generic_timer_init_secondary_cpu, arm_generic_timer_init_secondary_cpu,
                   LK_INIT_LEVEL_THREADING - 1, LK_INIT_FLAG_SECONDARY_CPUS)

static void arm_generic_timer_resume_cpu(uint level) {
  // Always trigger a timer interrupt on each cpu for now
  write_tval(0);
  write_ctl(1);
}

LK_INIT_HOOK_FLAGS(arm_generic_timer_resume_cpu, arm_generic_timer_resume_cpu,
                   LK_INIT_LEVEL_PLATFORM, LK_INIT_FLAG_CPU_RESUME)

static void arm_generic_timer_pdev_init(const void* driver_data, uint32_t length) {
  ASSERT(length >= sizeof(dcfg_arm_generic_timer_driver_t));
  auto driver = static_cast<const dcfg_arm_generic_timer_driver_t*>(driver_data);
  uint32_t irq_phys = driver->irq_phys;
  uint32_t irq_virt = driver->irq_virt;
  uint32_t irq_sphys = driver->irq_sphys;
  size_t entry_ticks_idx;

  if (irq_phys && irq_virt && arm64_get_boot_el() < 2) {
    // If we did not boot at EL2 or above, prefer the virtual timer.
    irq_phys = 0;
  }
  const char* timer_str = "";
  if (irq_phys) {
    timer_str = "phys";
    timer_irq = irq_phys;
    timer_assignment = IRQ_PHYS;
    reg_procs = &cntp_procs;
    entry_ticks_idx = 0;
  } else if (irq_virt) {
    timer_str = "virt";
    timer_irq = irq_virt;
    timer_assignment = IRQ_VIRT;
    reg_procs = &cntv_procs;
    entry_ticks_idx = 1;
  } else if (irq_sphys) {
    timer_str = "sphys";
    timer_irq = irq_sphys;
    timer_assignment = IRQ_SPHYS;
    reg_procs = &cntps_procs;
    entry_ticks_idx = 0;
  } else {
    panic("no irqs set in arm_generic_timer_pdev_init\n");
  }
  arch::ThreadMemoryBarrier();

  timeline_zbi_entry.Set(kernel_entry_ticks[entry_ticks_idx]);
  timeline_virtual_entry.Set(kernel_virtual_entry_ticks[entry_ticks_idx]);

  dprintf(INFO, "arm generic timer using %s timer, irq %d\n", timer_str, timer_irq);

  arm_generic_timer_init(driver->freq_override);
}

// Called once per cpu in the system post cpu detection.
static void late_update_reg_procs(uint) {
  ASSERT(timer_assignment == IRQ_PHYS || timer_assignment == IRQ_VIRT ||
         timer_assignment == IRQ_SPHYS);

  // If at least one of the cpus are an A73, switch the read hooks to a specialized
  // A73 errata workaround version. Note this will duplicately run on every
  // core in the system.
  if (arm64_read_percpu_ptr()->microarch == ARM_CORTEX_A73) {
    if (timer_assignment == IRQ_PHYS) {
      reg_procs = &cntp_procs_a73;
    } else if (timer_assignment == IRQ_VIRT) {
      reg_procs = &cntv_procs_a73;
    } else if (timer_assignment == IRQ_SPHYS) {
      reg_procs = &cntps_procs_a73;
    } else {
      panic("no irqs set in late_update_reg_procs\n");
    }

    ktl::atomic_thread_fence(ktl::memory_order_seq_cst);

    dprintf(INFO, "arm generic timer applying A73 workaround\n");
  }
}

LK_PDEV_INIT(arm_generic_timer_pdev_init, KDRV_ARM_GENERIC_TIMER, arm_generic_timer_pdev_init,
             LK_INIT_LEVEL_PLATFORM_EARLY)

LK_INIT_HOOK_FLAGS(late_update_reg_procs, &late_update_reg_procs, LK_INIT_LEVEL_PLATFORM_EARLY + 1,
                   LK_INIT_FLAG_ALL_CPUS)

/********************************************************************************
 *
 * Tests
 *
 ********************************************************************************/

namespace {

[[maybe_unused]] constexpr uint32_t kMinTestFreq = 1;
[[maybe_unused]] constexpr uint32_t kMaxTestFreq = ktl::numeric_limits<uint32_t>::max();
[[maybe_unused]] constexpr uint32_t kCurTestFreq = 0;

inline uint64_t abs_int64(int64_t a) { return (a > 0) ? a : -a; }

bool test_time_conversion_check_result(uint64_t a, uint64_t b, uint64_t limit) {
  BEGIN_TEST;

  if (a != b) {
    uint64_t diff = abs_int64(a - b);
    ASSERT_LE(diff, limit);
  }

  END_TEST;
}

#if 0
static void test_zx_time_to_cntpct(uint32_t cntfrq, zx_time_t zx_time) {
    uint64_t cntpct = zx_time_to_cntpct(zx_time);
    const uint64_t nanos_per_sec = ZX_SEC(1);
    uint64_t expected_cntpct = ((uint64_t)cntfrq * zx_time + nanos_per_sec / 2) / nanos_per_sec;

    test_time_conversion_check_result(cntpct, expected_cntpct, 1);
    LTRACEF_LEVEL(2, "zx_time_to_cntpct(%" PRIi64 "): got %" PRIu64
                     ", expect %" PRIu64 "\n",
                  zx_time, cntpct, expected_cntpct);
}
#endif

bool test_time_to_cntpct(uint32_t cntfrq) {
  BEGIN_TEST;

  affine::Ratio time_to_ticks;
  if (cntfrq == kCurTestFreq) {
    uint64_t tps = ticks_per_second();
    ASSERT_LE(tps, ktl::numeric_limits<uint32_t>::max());
    cntfrq = static_cast<uint32_t>(tps);
    time_to_ticks = platform_get_ticks_to_time_ratio().Inverse();
  } else {
    time_to_ticks = arm_generic_timer_compute_conversion_factors(cntfrq).Inverse();
  }

  constexpr uint64_t VECTORS[] = {
      0,
      1,
      60 * 60 * 24,
      60 * 60 * 24 * 365,
      60 * 60 * 24 * (365 * 10 + 2),
      60ULL * 60 * 24 * (365 * 100 + 2),
  };

  for (auto vec : VECTORS) {
    uint64_t cntpct = time_to_ticks.Scale(vec);
    constexpr uint32_t nanos_per_sec = ZX_SEC(1);
    uint64_t expected_cntpct = ((uint64_t)cntfrq * vec + (nanos_per_sec / 2)) / nanos_per_sec;

    if (!test_time_conversion_check_result(cntpct, expected_cntpct, 1)) {
      printf("FAIL: zx_time_to_cntpct(%" PRIu64 "): got %" PRIu64 ", expect %" PRIu64 "\n", vec,
             cntpct, expected_cntpct);
      ASSERT_TRUE(false);
    }
  }

  END_TEST;
}

bool test_cntpct_to_time(uint32_t cntfrq) {
  BEGIN_TEST;

  affine::Ratio ticks_to_time;
  if (cntfrq == kCurTestFreq) {
    uint64_t tps = ticks_per_second();
    ASSERT_LE(tps, ktl::numeric_limits<uint32_t>::max());
    cntfrq = static_cast<uint32_t>(tps);
    ticks_to_time = platform_get_ticks_to_time_ratio();
  } else {
    ticks_to_time = arm_generic_timer_compute_conversion_factors(cntfrq);
  }

  constexpr uint64_t VECTORS[] = {
      1,
      60 * 60 * 24,
      60 * 60 * 24 * 365,
      60 * 60 * 24 * (365 * 10 + 2),
      60ULL * 60 * 24 * (365 * 50 + 2),
  };

  for (auto vec : VECTORS) {
    zx_time_t expected_zx_time = ZX_SEC(vec);
    uint64_t cntpct = (uint64_t)cntfrq * vec;
    zx_time_t zx_time = ticks_to_time.Scale(cntpct);

    const uint64_t limit = (1000 * 1000 + cntfrq - 1) / cntfrq;
    if (!test_time_conversion_check_result(zx_time, expected_zx_time, limit)) {
      printf("cntpct_to_zx_time(0x%" PRIx64 "): got 0x%" PRIx64 ", expect 0x%" PRIx64 "\n", cntpct,
             static_cast<uint64_t>(zx_time), static_cast<uint64_t>(expected_zx_time));
      ASSERT_TRUE(false);
    }
  }

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(arm_clock_tests)
UNITTEST("Time --> CNTPCT (min freq)", []() -> bool { return test_time_to_cntpct(kMinTestFreq); })
UNITTEST("Time --> CNTPCT (max freq)", []() -> bool { return test_time_to_cntpct(kMaxTestFreq); })
UNITTEST("Time --> CNTPCT (cur freq)", []() -> bool { return test_time_to_cntpct(kCurTestFreq); })
UNITTEST("CNTPCT --> Time (min freq)", []() -> bool { return test_cntpct_to_time(kMinTestFreq); })
UNITTEST("CNTPCT --> Time (max freq)", []() -> bool { return test_cntpct_to_time(kMaxTestFreq); })
UNITTEST("CNTPCT --> Time (cur freq)", []() -> bool { return test_cntpct_to_time(kCurTestFreq); })
UNITTEST_END_TESTCASE(arm_clock_tests, "arm_clock", "Tests for ARM tick count and current time")
