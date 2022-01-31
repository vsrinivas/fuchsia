// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>
#include <lib/acpi_lite.h>
#include <lib/acpi_lite/structures.h>
#include <lib/affine/ratio.h>
#include <lib/console.h>
#include <lib/fit/defer.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <fbl/algorithm.h>
#include <kernel/lockdep.h>
#include <kernel/spinlock.h>
#include <ktl/algorithm.h>
#include <ktl/limits.h>
#include <lk/init.h>
#include <platform/pc/acpi.h>
#include <platform/pc/hpet.h>
#include <vm/vm_aspace.h>

struct hpet_timer_registers {
  volatile uint64_t conf_caps;
  volatile uint64_t comparator_value;
  volatile uint64_t fsb_int_route;
  uint8_t _reserved[8];
} __PACKED;

struct hpet_registers {
  volatile uint64_t general_caps;
  uint8_t _reserved0[8];
  volatile uint64_t general_config;
  uint8_t _reserved1[8];
  volatile uint64_t general_int_status;
  uint8_t _reserved2[0xf0 - 0x28];
  volatile uint64_t main_counter_value;
  uint8_t _reserved3[8];
  struct hpet_timer_registers timers[];
} __PACKED;

DECLARE_SINGLETON_SPINLOCK(hpet_lock);

static bool hpet_present = false;
static struct hpet_registers* hpet_regs;
uint64_t _hpet_ticks_per_ms;
static uint8_t num_timers;

#define MAX_PERIOD_IN_FS 0x05F5E100ULL

/* Bit masks for the general_config register */
#define GEN_CONF_EN 1

/* Bit masks for the per-time conf_caps register */
#define TIMER_CONF_INT_EN (1ULL << 2)

static void hpet_init(uint level) {
  // Look up the HPET table.
  const acpi_lite::AcpiHpetTable* hpet_desc =
      acpi_lite::GetTableByType<acpi_lite::AcpiHpetTable>(GlobalAcpiLiteParser());
  if (hpet_desc == nullptr) {
    dprintf(INFO, "No HPET ACPI table found.\n");
    return;
  }

  // Ensure the HPET table uses MMIO.
  if (hpet_desc->address.address_space_id != ACPI_ADDR_SPACE_MEMORY) {
    dprintf(INFO, "HPET unsupported: require MMIO-based HPET.\n");
    return;
  }

  zx_status_t res = VmAspace::kernel_aspace()->AllocPhysical(
      "hpet", PAGE_SIZE,          /* size */
      (void**)&hpet_regs,         /* returned virtual address */
      PAGE_SIZE_SHIFT,            /* alignment log2 */
      hpet_desc->address.address, /* physical address */
      0,                          /* vmm flags */
      ARCH_MMU_FLAG_UNCACHED_DEVICE | ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE);
  if (res != ZX_OK) {
    return;
  }

  // If something goes wrong, make sure we free the HPET registers.
  auto cleanup = fit::defer([]() {
    VmAspace::kernel_aspace()->FreeRegion(reinterpret_cast<vaddr_t>(hpet_regs));
    hpet_regs = nullptr;
    num_timers = 0;
  });

  bool has_64bit_count = BIT_SET(hpet_regs->general_caps, 13);
  uint64_t tick_period_in_fs = hpet_regs->general_caps >> 32;
  if (tick_period_in_fs == 0 || tick_period_in_fs > MAX_PERIOD_IN_FS) {
    return;
  }

  /* We only support HPETs that are 64-bit and have at least two timers */
  num_timers = static_cast<uint8_t>(BITS_SHIFT(hpet_regs->general_caps, 12, 8) + 1);
  if (!has_64bit_count || num_timers < 2) {
    return;
  }

  /* Make sure all timers have interrupts disabled */
  for (uint8_t i = 0; i < num_timers; ++i) {
    hpet_regs->timers[i].conf_caps = hpet_regs->timers[i].conf_caps & ~TIMER_CONF_INT_EN;
  }

  // Figure out the nominal ratio of clock monotonic ticks (nsec) to HPET ticks.
  // This is the scaling factor when converting from HPET to clock monotonic.
  // Unfortunately, the HPET's rate is reported by the registers as a nominal
  // period (in femtosecond) instead of a nominal frequency (in Hz, or even
  // mHz).
  //
  // In the real world, the HPET is most likely running at the bus-issue rate
  // for the motherboard (24MHz, 100MHz, etc) or the CPU issue rate (2.4GHz, 4.0
  // GHZ, etc), meaning that the nominal period reported is off by some fraction
  // of a femtosecond because the nominal frequency of the counter does not
  // perfectly divide the value 10^15.
  //
  // For example, when the actual nominal HPET rate is 24MHz, the value which
  // will be reported by the register is 41,666,667 instead of the more precise
  // 41,666,666 + 2/3 (the actual nominal ratio).
  //
  // So: when computing the HPET -> clock monotonic ticks ratio, assume that the
  // underlying period actually comes from a clock expressed as an integer
  // number of Hz, and try to reconstruct that frequency from the reported
  // period by dividing and rounding up instead of rounding down.
  constexpr uint64_t val10e15 = 1'000'000'000'000'000;
  const uint64_t hpet_nominal_frequency = (val10e15 + tick_period_in_fs - 1) / tick_period_in_fs;

  uint64_t N = 1'000'000'000;
  uint64_t D = hpet_nominal_frequency;
  affine::Ratio::Reduce(&N, &D);

  // If ratio between HPET's rate and clock monotonic's rate cannot be stored
  // in a 32 bit integer, then we cannot use HPET as our reference timer.
  if ((N > ktl::numeric_limits<uint32_t>::max()) || (D > ktl::numeric_limits<uint32_t>::max())) {
    printf(
        "HPET to clock monotonic rate ratio (%lu/%lu) cannot be stored as a 32 bit ratio! Ignoring "
        "HPET\n",
        N, D);
    return;
  }

  hpet_ticks_to_clock_monotonic = {static_cast<uint32_t>(N), static_cast<uint32_t>(D)};
  _hpet_ticks_per_ms = static_cast<uint64_t>(hpet_nominal_frequency) / 1000;
  hpet_present = true;

  dprintf(INFO, "HPET: detected at %#" PRIx64 " ticks per ms %" PRIu64 " num timers %hhu\n",
          hpet_desc->address.address, _hpet_ticks_per_ms, num_timers);

  // things went well, cancel our cleanup auto-call
  cleanup.cancel();
}

/* Begin running after ACPI tables are up */
LK_INIT_HOOK(hpet, hpet_init, LK_INIT_LEVEL_VM + 2)

uint64_t hpet_get_value(void) {
  uint64_t v = hpet_regs->main_counter_value;
  uint64_t v2 = hpet_regs->main_counter_value;
  /* Even though the specification says it should not be necessary to read
   * multiple times, we have observed that QEMU converts the 64-bit
   * memory access in to two 32-bit accesses, resulting in bad reads. QEMU
   * reads the low 32-bits first, so the result is a large jump when it
   * wraps 32 bits.  To work around this, we return the lesser of two reads.
   */
  return ktl::min(v, v2);
}

zx_status_t hpet_set_value(uint64_t v) {
  Guard<SpinLock, NoIrqSave> guard{hpet_lock::Get()};

  if (hpet_regs->general_config & GEN_CONF_EN) {
    return ZX_ERR_BAD_STATE;
  }

  hpet_regs->main_counter_value = v;
  return ZX_OK;
}

bool hpet_is_present(void) { return hpet_present; }

void hpet_enable(void) {
  DEBUG_ASSERT(hpet_is_present());

  Guard<SpinLock, NoIrqSave> guard{hpet_lock::Get()};
  hpet_regs->general_config = hpet_regs->general_config | GEN_CONF_EN;
}

void hpet_disable(void) {
  DEBUG_ASSERT(hpet_is_present());

  Guard<SpinLock, NoIrqSave> guard{hpet_lock::Get()};
  hpet_regs->general_config = hpet_regs->general_config & ~GEN_CONF_EN;
}

/* Blocks for the requested number of milliseconds.
 * For use in calibration */
void hpet_wait_ms(uint16_t ms) {
  uint64_t init_timer_value = hpet_regs->main_counter_value;
  uint64_t target = (uint64_t)ms * _hpet_ticks_per_ms;
  while (hpet_regs->main_counter_value - init_timer_value <= target)
    ;
}

namespace {

int cmd_show_hpet_regs() {
  if (!hpet_is_present()) {
    printf("HPET is not present.\n");
    return -1;
  }

  if (hpet_regs == nullptr) {
    printf("HPET registers are NULL.\n");
    return -1;
  }

  auto Dump = [](uint64_t reg_val, uint32_t high_bit, uint32_t low_bit, const char* name) {
    uint64_t mask = (static_cast<uint64_t>(0x1) << (high_bit - low_bit + 1)) - 1;
    uint64_t val = (reg_val >> low_bit) & mask;
    printf("%16s : 0x%lx (%lu)\n", name, val, val);
  };

  printf("HPET registers are mapped at %p\n", hpet_regs);
  Dump(hpet_regs->general_caps, 63, 0, "CAPS (all)");
  Dump(hpet_regs->general_caps, 63, 32, "CLK_PERIOD");
  Dump(hpet_regs->general_caps, 31, 16, "VENDOR_ID");
  Dump(hpet_regs->general_caps, 15, 15, "LEG_RT_CAP");
  Dump(hpet_regs->general_caps, 13, 13, "COUNT_SIZE_CAP");
  Dump(hpet_regs->general_caps, 12, 8, "NUM_TIM_CAP");
  Dump(hpet_regs->general_caps, 7, 0, "REV_ID");
  printf("\n");
  Dump(hpet_regs->general_config, 63, 0, "CONFIG (all)");
  Dump(hpet_regs->general_config, 1, 1, "LEG_RT_CNF");
  Dump(hpet_regs->general_config, 0, 0, "ENABLE_CNF");
  printf("\n");
  Dump(hpet_regs->general_int_status, 63, 0, "INT_STS (all)");
  printf("\n");
  Dump(hpet_regs->general_int_status, 63, 0, "COUNT");

  return 0;
}

int cmd_hpet(int argc, const cmd_args* argv, uint32_t flags) {
  auto usage = [prog_name = argv[0].str]() -> int {
    printf("Usage:\n");
    printf("%s regs : show the HPET registers\n", prog_name);
    return -1;
  };

  if (argc < 2) {
    return usage();
  }

  if (!strcmp(argv[1].str, "regs")) {
    return cmd_show_hpet_regs();
  } else {
    printf("Unrecognized command \"%s\".\n", argv[1].str);
    return usage();
  }
}
}  // namespace

STATIC_COMMAND_START
STATIC_COMMAND_MASKED("hpet", "HPET commands", &cmd_hpet, CMD_AVAIL_ALWAYS)
STATIC_COMMAND_END(kernel)
