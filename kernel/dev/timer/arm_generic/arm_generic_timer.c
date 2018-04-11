// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2013, Google Inc. All rights reserved.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#include <arch/ops.h>
#include <assert.h>
#include <inttypes.h>
#include <lk/init.h>
#include <platform.h>
#include <dev/interrupt.h>
#include <dev/timer/arm_generic.h>
#include <platform/timer.h>
#include <trace.h>
#include <zircon/types.h>

#include <pdev/driver.h>
#include <zircon/boot/driver-config.h>

#define LOCAL_TRACE 0

#include <lib/fixed_point.h>

/* CNTFRQ AArch64 register */
#define TIMER_REG_CNTFRQ    cntfrq_el0

/* CNTP AArch64 registers */
#define TIMER_REG_CNTP_CTL  cntp_ctl_el0
#define TIMER_REG_CNTP_CVAL cntp_cval_el0
#define TIMER_REG_CNTP_TVAL cntp_tval_el0
#define TIMER_REG_CNTPCT    cntpct_el0

/* CNTPS AArch64 registers */
#define TIMER_REG_CNTPS_CTL cntps_ctl_el1
#define TIMER_REG_CNTPS_CVAL    cntps_cval_el1
#define TIMER_REG_CNTPS_TVAL    cntps_tval_el1

/* CNTV AArch64 registers */
#define TIMER_REG_CNTV_CTL  cntv_ctl_el0
#define TIMER_REG_CNTV_CVAL cntv_cval_el0
#define TIMER_REG_CNTV_TVAL cntv_tval_el0
#define TIMER_REG_CNTVCT    cntvct_el0

static int timer_irq;

struct fp_32_64 cntpct_per_ns;
struct fp_32_64 ns_per_cntpct;

static uint64_t zx_time_to_cntpct(zx_time_t zx_time)
{
    return u64_mul_u64_fp32_64(zx_time, cntpct_per_ns);
}

zx_time_t cntpct_to_zx_time(uint64_t cntpct)
{
    return u64_mul_u64_fp32_64(cntpct, ns_per_cntpct);
}

static uint32_t read_cntfrq(void)
{
    uint32_t cntfrq;

    cntfrq = ARM64_READ_SYSREG(TIMER_REG_CNTFRQ);
    LTRACEF("cntfrq: 0x%08x, %u\n", cntfrq, cntfrq);
    return cntfrq;
}

static uint32_t read_cntp_ctl(void)
{
    return ARM64_READ_SYSREG(TIMER_REG_CNTP_CTL);
}

static uint32_t read_cntv_ctl(void)
{
    return ARM64_READ_SYSREG(TIMER_REG_CNTV_CTL);
}

static uint32_t read_cntps_ctl(void)
{
    return ARM64_READ_SYSREG(TIMER_REG_CNTPS_CTL);
}

static void write_cntp_ctl(uint32_t val)
{
    LTRACEF_LEVEL(3, "cntp_ctl: 0x%x %x\n", val, read_cntp_ctl());
    ARM64_WRITE_SYSREG(TIMER_REG_CNTP_CTL, val);
}

static void write_cntv_ctl(uint32_t val)
{
    LTRACEF_LEVEL(3, "cntv_ctl: 0x%x %x\n", val, read_cntv_ctl());
    ARM64_WRITE_SYSREG(TIMER_REG_CNTV_CTL, val);
}

static void write_cntps_ctl(uint32_t val)
{
    LTRACEF_LEVEL(3, "cntps_ctl: 0x%x %x\n", val, read_cntps_ctl());
    ARM64_WRITE_SYSREG(TIMER_REG_CNTPS_CTL, val);
}

static void write_cntp_cval(uint64_t val)
{
    LTRACEF_LEVEL(3, "cntp_cval: 0x%016" PRIx64 ", %" PRIu64 "\n",
                  val, val);
    ARM64_WRITE_SYSREG(TIMER_REG_CNTP_CVAL, val);
}

static void write_cntv_cval(uint64_t val)
{
    LTRACEF_LEVEL(3, "cntv_cval: 0x%016" PRIx64 ", %" PRIu64 "\n",
                  val, val);
    ARM64_WRITE_SYSREG(TIMER_REG_CNTV_CVAL, val);
}

static void write_cntps_cval(uint64_t val)
{
    LTRACEF_LEVEL(3, "cntps_cval: 0x%016" PRIx64 ", %" PRIu64 "\n",
                  val, val);
    ARM64_WRITE_SYSREG(TIMER_REG_CNTPS_CVAL, val);
}

static void write_cntp_tval(int32_t val)
{
    LTRACEF_LEVEL(3, "cntp_tval: %d\n", val);
    ARM64_WRITE_SYSREG(TIMER_REG_CNTP_TVAL, val);
}

static void write_cntv_tval(int32_t val)
{
    LTRACEF_LEVEL(3, "cntv_tval: %d\n", val);
    ARM64_WRITE_SYSREG(TIMER_REG_CNTV_TVAL, val);
}

static void write_cntps_tval(int32_t val)
{
    LTRACEF_LEVEL(3, "cntps_tval: %d\n", val);
    ARM64_WRITE_SYSREG(TIMER_REG_CNTPS_TVAL, val);
}

static uint64_t read_cntpct(void) {
    return ARM64_READ_SYSREG(TIMER_REG_CNTPCT);
}

static uint64_t read_cntvct(void) {
    return ARM64_READ_SYSREG(TIMER_REG_CNTVCT);
}

struct timer_reg_procs {
    void (*write_ctl)(uint32_t val);
    void (*write_cval)(uint64_t val);
    void (*write_tval)(int32_t val);
    uint64_t (*read_ct)(void);
};

__UNUSED static const struct timer_reg_procs cntp_procs = {
    .write_ctl = write_cntp_ctl,
    .write_cval = write_cntp_cval,
    .write_tval = write_cntp_tval,
    .read_ct = read_cntpct,
};

__UNUSED static const struct timer_reg_procs cntv_procs = {
    .write_ctl = write_cntv_ctl,
    .write_cval = write_cntv_cval,
    .write_tval = write_cntv_tval,
    .read_ct = read_cntvct,
};

__UNUSED static const struct timer_reg_procs cntps_procs = {
    .write_ctl = write_cntps_ctl,
    .write_cval = write_cntps_cval,
    .write_tval = write_cntps_tval,
    .read_ct = read_cntpct,
};

#if (TIMER_ARM_GENERIC_SELECTED_CNTV)
static const struct timer_reg_procs* reg_procs = &cntv_procs;
#else
static const struct timer_reg_procs* reg_procs = &cntp_procs;
#endif

static inline void write_ctl(uint32_t val) {
    reg_procs->write_ctl(val);
}

static inline void write_cval(uint64_t val)
{
    reg_procs->write_cval(val);
}

static inline void write_tval(uint32_t val)
{
    reg_procs->write_tval(val);
}

static uint64_t read_ct(void)
{
    uint64_t cntpct = reg_procs->read_ct();
    LTRACEF_LEVEL(3, "cntpct: 0x%016" PRIx64 ", %" PRIu64 "\n",
                  cntpct, cntpct);
    return cntpct;
}

static void platform_tick(void *arg)
{
    write_ctl(0);
    timer_tick(current_time());
}

zx_status_t platform_set_oneshot_timer(zx_time_t deadline)
{
    DEBUG_ASSERT(arch_ints_disabled());

    // Add one to the deadline, since with very high probability the deadline
    // straddles a counter tick.
    const uint64_t cntpct_deadline = zx_time_to_cntpct(deadline) + 1;

    // Even if the deadline has already passed, the ARMv8-A timer will fire the
    // interrupt.
    write_cval(cntpct_deadline);
    write_ctl(1);

    return 0;
}

void platform_stop_timer(void)
{
    write_ctl(0);
}

zx_time_t current_time(void)
{
    return cntpct_to_zx_time(read_ct());
}

zx_ticks_t current_ticks(void)
{
    return read_ct();
}

zx_ticks_t ticks_per_second(void)
{
    return u64_mul_u32_fp32_64(1000 * 1000 * 1000, cntpct_per_ns);
}

static uint32_t abs_int32(int32_t a)
{
    return (a > 0) ? a : -a;
}

static uint64_t abs_int64(int64_t a)
{
    return (a > 0) ? a : -a;
}

static void test_time_conversion_check_result(uint64_t a, uint64_t b, uint64_t limit, bool is32)
{
    if (a != b) {
        uint64_t diff = is32 ? abs_int32(a - b) : abs_int64(a - b);
        if (diff <= limit)
            LTRACEF("ROUNDED by %" PRIu64 " (up to %" PRIu64 " allowed)\n"
                    , diff, limit);
        else
            TRACEF("FAIL, off by %" PRIu64 "\n", diff);
    }
}

static void test_zx_time_to_cntpct(uint32_t cntfrq, zx_time_t zx_time)
{
    uint64_t cntpct = zx_time_to_cntpct(zx_time);
    const uint64_t nanos_per_sec = ZX_SEC(1);
    uint64_t expected_cntpct = ((uint64_t)cntfrq * zx_time + nanos_per_sec / 2) / nanos_per_sec;

    test_time_conversion_check_result(cntpct, expected_cntpct, 1, false);
    LTRACEF_LEVEL(2, "zx_time_to_cntpct(%" PRIu64 "): got %" PRIu64
                  ", expect %" PRIu64 "\n",
                  zx_time, cntpct, expected_cntpct);
}

static void test_cntpct_to_zx_time(uint32_t cntfrq, uint64_t expected_s)
{
    zx_time_t expected_zx_time = ZX_SEC(expected_s);
    uint64_t cntpct = (uint64_t)cntfrq * expected_s;
    zx_time_t zx_time = cntpct_to_zx_time(cntpct);

    test_time_conversion_check_result(zx_time, expected_zx_time, (1000 * 1000 + cntfrq - 1) / cntfrq, false);
    LTRACEF_LEVEL(2, "cntpct_to_zx_time(%" PRIu64
                  "): got %" PRIu64 ", expect %" PRIu64 "\n",
                  cntpct, zx_time, expected_zx_time);
}

static void test_time_conversions(uint32_t cntfrq)
{
    test_zx_time_to_cntpct(cntfrq, 0);
    test_zx_time_to_cntpct(cntfrq, 1);
    test_zx_time_to_cntpct(cntfrq, 60 * 60 * 24);
    test_zx_time_to_cntpct(cntfrq, 60 * 60 * 24 * 365);
    test_zx_time_to_cntpct(cntfrq, 60 * 60 * 24 * (365 * 10 + 2));
    test_zx_time_to_cntpct(cntfrq, 60ULL * 60 * 24 * (365 * 100 + 2));
    test_zx_time_to_cntpct(cntfrq, 1ULL<<60);
    test_cntpct_to_zx_time(cntfrq, 0);
    test_cntpct_to_zx_time(cntfrq, 1);
    test_cntpct_to_zx_time(cntfrq, 60 * 60 * 24);
    test_cntpct_to_zx_time(cntfrq, 60 * 60 * 24 * 365);
    test_cntpct_to_zx_time(cntfrq, 60 * 60 * 24 * (365 * 10 + 2));
    test_cntpct_to_zx_time(cntfrq, 60ULL * 60 * 24 * (365 * 100 + 2));
}

static void arm_generic_timer_init_conversion_factors(uint32_t cntfrq)
{
    fp_32_64_div_32_32(&cntpct_per_ns, cntfrq, ZX_SEC(1));
    fp_32_64_div_32_32(&ns_per_cntpct, ZX_SEC(1), cntfrq);
    dprintf(SPEW, "cntpct_per_ns: %08x.%08x%08x\n", cntpct_per_ns.l0, cntpct_per_ns.l32, cntpct_per_ns.l64);
    dprintf(SPEW, "ns_per_cntpct: %08x.%08x%08x\n", ns_per_cntpct.l0, ns_per_cntpct.l32, ns_per_cntpct.l64);
}

static void arm_generic_timer_init(uint32_t freq_override)
{
    uint32_t cntfrq;

    if (freq_override == 0) {
        cntfrq = read_cntfrq();

        if (!cntfrq) {
            TRACEF("Failed to initialize timer, frequency is 0\n");
            return;
        }
    } else {
        cntfrq = freq_override;
    }

    dprintf(INFO, "arm generic timer freq %u Hz\n", cntfrq);

#if LOCAL_TRACE
    LTRACEF("Test min cntfrq\n");
    arm_generic_timer_init_conversion_factors(1);
    test_time_conversions(1);
    LTRACEF("Test max cntfrq\n");
    arm_generic_timer_init_conversion_factors(~0);
    test_time_conversions(~0);
    LTRACEF("Set actual cntfrq\n");
#endif
    arm_generic_timer_init_conversion_factors(cntfrq);
    test_time_conversions(cntfrq);

    LTRACEF("register irq %d on cpu %u\n", timer_irq, arch_curr_cpu_num());
    zx_status_t status = register_int_handler(timer_irq, &platform_tick, NULL);
    DEBUG_ASSERT(status == ZX_OK);
    unmask_interrupt(timer_irq);
}

static void arm_generic_timer_init_secondary_cpu(uint level)
{
    LTRACEF("unmask irq %d on cpu %u\n", timer_irq, arch_curr_cpu_num());
    unmask_interrupt(timer_irq);
}

/* secondary cpu initialize the timer just before the kernel starts with interrupts enabled */
LK_INIT_HOOK_FLAGS(arm_generic_timer_init_secondary_cpu,
                   arm_generic_timer_init_secondary_cpu,
                   LK_INIT_LEVEL_THREADING - 1, LK_INIT_FLAG_SECONDARY_CPUS);

static void arm_generic_timer_resume_cpu(uint level)
{
    /* Always trigger a timer interrupt on each cpu for now */
    write_tval(0);
    write_ctl(1);
}

LK_INIT_HOOK_FLAGS(arm_generic_timer_resume_cpu, arm_generic_timer_resume_cpu,
                   LK_INIT_LEVEL_PLATFORM, LK_INIT_FLAG_CPU_RESUME);

static void arm_generic_timer_pdev_init(const void* driver_data, uint32_t length) {
    ASSERT(length >= sizeof(dcfg_arm_generic_timer_driver_t));
    const dcfg_arm_generic_timer_driver_t* driver = driver_data;
    uint32_t irq_phys = driver->irq_phys;
    uint32_t irq_virt = driver->irq_virt;
    uint32_t irq_sphys = driver->irq_sphys;

    if (irq_phys && irq_virt && arm64_get_boot_el() < 2) {
        // If we did not boot at EL2 or above, prefer the virtual timer.
        irq_phys = 0;
    }
    if (irq_phys) {
        timer_irq = irq_phys;
        reg_procs = &cntp_procs;
    } else if (irq_virt) {
        timer_irq = irq_virt;
        reg_procs = &cntv_procs;
    } else if (irq_sphys) {
        timer_irq = irq_sphys;
        reg_procs = &cntps_procs;
    } else {
        panic("no irqs set in arm_generic_timer_pdev_init\n");
    }
    smp_mb();

    arm_generic_timer_init(driver->freq_override);
}

LK_PDEV_INIT(arm_generic_timer_pdev_init, KDRV_ARM_GENERIC_TIMER, arm_generic_timer_pdev_init, LK_INIT_LEVEL_PLATFORM_EARLY);
