// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// A note on terminology: "events" vs "counters": A "counter" is an
// "event", but some events are not counters. Internally, we use the
// term "counter" when we know the event is a counter.
//
// TODO(fxbug.dev/33108): combine common parts with x86 (after things settle)
// TODO(fxbug.dev/33109): chain event handling

#include <assert.h>
#include <err.h>
#include <lib/perfmon.h>
#include <lib/zircon-internal/mtrace.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <platform.h>
#include <string.h>
#include <trace.h>

#include <new>

#include <arch/arch_ops.h>
#include <arch/arm64.h>
#include <arch/arm64/perf_mon.h>
#include <arch/mmu.h>
#include <dev/interrupt/arm_gic_common.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>
#include <kernel/align.h>
#include <kernel/cpu.h>
#include <kernel/mp.h>
#include <kernel/mutex.h>
#include <kernel/stats.h>
#include <kernel/thread.h>
#include <ktl/move.h>
#include <ktl/unique_ptr.h>
#include <lk/init.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object_physical.h>

#define LOCAL_TRACE 0

static void arm64_perfmon_reset_task(void* raw_context);

static constexpr int kProgrammableCounterWidth = 32;
static constexpr int kFixedCounterWidth = 64;
static constexpr uint32_t kMaxProgrammableCounterValue = UINT32_MAX;
static constexpr uint64_t kMaxFixedCounterValue = UINT64_MAX;

static bool perfmon_hw_initialized = false;

static uint32_t perfmon_imp = 0;
static uint16_t perfmon_version = 0;

static uint16_t perfmon_num_programmable_counters = 0;
static uint16_t perfmon_num_fixed_counters = 0;

// Counter bits in PMOVS{CLR,SET} to check on each interrupt.
static uint32_t perfmon_counter_status_bits = 0;

namespace {

struct PerfmonState : public PerfmonStateBase {
  static zx_status_t Create(unsigned n_cpus, ktl::unique_ptr<PerfmonState>* out_state);
  explicit PerfmonState(unsigned n_cpus);

  // The value of the pmcr register.
  // TODO(dje): Review access to cycle counter, et.al., when not
  // collecting data.
  uint32_t pmcr_el0 = 0;

  // See arm64-pm.h:Arm64PmuConfig.
  PmuEventId timebase_event = perfmon::kEventIdNone;

  // The number of each kind of event in use, so we don't have to iterate
  // over the entire arrays.
  unsigned num_used_fixed = 0;
  unsigned num_used_programmable = 0;

  // The ids for each of the in-use events, or zero if not used.
  // These are passed in from the driver and then written to the buffer,
  // but otherwise have no meaning to us.
  // All in-use entries appear consecutively.
  PmuEventId fixed_events[ARM64_PMU_MAX_FIXED_COUNTERS] = {};
  PmuEventId programmable_events[ARM64_PMU_MAX_PROGRAMMABLE_COUNTERS] = {};

  // The counters are reset to this at the start.
  // And again for those that are reset on overflow.
  uint64_t fixed_initial_value[ARM64_PMU_MAX_FIXED_COUNTERS] = {};
  uint32_t programmable_initial_value[ARM64_PMU_MAX_PROGRAMMABLE_COUNTERS] = {};

  // Flags for each event/counter, perfmon::kPmuConfigFlag*.
  uint32_t fixed_flags[ARM64_PMU_MAX_FIXED_COUNTERS] = {};
  uint32_t programmable_flags[ARM64_PMU_MAX_PROGRAMMABLE_COUNTERS] = {};

  // PMCCFILTR (the cycle counter control register)
  uint32_t fixed_hw_events[ARM64_PMU_MAX_FIXED_COUNTERS] = {};
  // PMEVTYPER<n>
  uint32_t programmable_hw_events[ARM64_PMU_MAX_PROGRAMMABLE_COUNTERS] = {};

  // The value to write to PMCNTEN{CLR,SET}_EL0, PMOVS{CLR,SET}_EL0.
  // This is 1 for all the counters in use.
  uint32_t pm_counter_ctrl = 0;

  // The value to write to PMINTENSET_EL1.
  // This is 1 for all the counters that should trigger interrupts,
  // which is not necessarily all the counters in use.
  uint32_t pmintenset_el1 = 0;
};

DECLARE_SINGLETON_MUTEX(PerfmonLock);

}  // namespace

static ktl::unique_ptr<PerfmonState> perfmon_state TA_GUARDED(PerfmonLock::Get());

static inline void enable_counters(PerfmonState* state) {
  __arm_wsr64("pmcr_el0", state->pmcr_el0);
}

static inline void disable_counters() { __arm_wsr64("pmcr_el0", 0); }

zx_status_t PerfmonState::Create(unsigned n_cpus, ktl::unique_ptr<PerfmonState>* out_state) {
  fbl::AllocChecker ac;
  auto state = ktl::unique_ptr<PerfmonState>(new (&ac) PerfmonState(n_cpus));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  if (!state->AllocatePerCpuData()) {
    return ZX_ERR_NO_MEMORY;
  }

  *out_state = ktl::move(state);
  return ZX_OK;
}

PerfmonState::PerfmonState(unsigned n_cpus) : PerfmonStateBase(n_cpus) {}

static void arm64_perfmon_init_once(uint level) {
  uint64_t pmcr = __arm_rsr64("pmcr_el0");

  // Play it safe for now and require ARM's implementation.
  if (((pmcr & ARM64_PMCR_EL0_IMP_MASK) >> ARM64_PMCR_EL0_IMP_SHIFT) != ARM64_PMCR_IMP_ARM) {
    return;
  }

  perfmon_imp = (pmcr & ARM64_PMCR_EL0_IMP_MASK) >> ARM64_PMCR_EL0_IMP_SHIFT;
  uint32_t idcode = (pmcr & ARM64_PMCR_EL0_IDCODE_MASK) >> ARM64_PMCR_EL0_IDCODE_SHIFT;
  if (idcode != 3) {
    // For now only support version 3.
    TRACEF("Unexpected/unsupported PMU idcode: 0x%x\n", idcode);
    return;
  }
  perfmon_version = 3;

  perfmon_num_programmable_counters = (pmcr & ARM64_PMCR_EL0_N_MASK) >> ARM64_PMCR_EL0_N_SHIFT;
  if (perfmon_num_programmable_counters > ARM64_PMU_MAX_PROGRAMMABLE_COUNTERS) {
    TRACEF("Clipping max number of programmable counters to %u\n",
           ARM64_PMU_MAX_PROGRAMMABLE_COUNTERS);
    perfmon_num_programmable_counters = ARM64_PMU_MAX_PROGRAMMABLE_COUNTERS;
  }

  // At the moment the architecture only has one fixed counter
  // (the cycle counter).
  perfmon_num_fixed_counters = 1;
  DEBUG_ASSERT(perfmon_num_fixed_counters <= ARM64_PMU_MAX_FIXED_COUNTERS);

  perfmon_supported = true;

  perfmon_counter_status_bits =
      (ARM64_PMOVSCLR_EL0_C_MASK | ((1 << perfmon_num_programmable_counters) - 1));

  // Note: The IRQ handler is configured separately.
  // If we don't have an IRQ (or a usable one - ZX-3302) then we can still
  // use tally mode and leave it to an external entity to periodically
  // collect the data.

  printf("ARM64 PMU: implementation 0x%x, version %u\n", perfmon_imp, perfmon_version);
  printf("ARM64 PMU: %u fixed counter(s), %u programmable counter(s)\n", perfmon_num_fixed_counters,
         perfmon_num_programmable_counters);
}

LK_INIT_HOOK(arm64_perfmon, arm64_perfmon_init_once, LK_INIT_LEVEL_ARCH)

static void arm64_perfmon_clear_overflow_indicators() {
  __arm_wsr64("pmovsclr_el0", perfmon_counter_status_bits);
}

size_t get_max_space_needed_for_all_records(PerfmonState* state) {
  size_t num_events = (state->num_used_programmable + state->num_used_fixed);
  return (sizeof(perfmon::TimeRecord) + num_events * kMaxEventRecordSize);
}

zx_status_t arch_perfmon_get_properties(ArchPmuProperties* props) {
  Guard<Mutex> guard(PerfmonLock::Get());

  if (!perfmon_supported) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  *props = {};
  props->common.pm_version = perfmon_version;
  props->common.max_num_fixed_events = perfmon_num_fixed_counters;
  props->common.max_num_programmable_events = perfmon_num_programmable_counters;
  props->common.max_fixed_counter_width = kFixedCounterWidth;
  props->common.max_programmable_counter_width = kProgrammableCounterWidth;

  return ZX_OK;
}

zx_status_t arch_perfmon_init() {
  Guard<Mutex> guard(PerfmonLock::Get());

  if (!perfmon_supported) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (atomic_load(&perfmon_active)) {
    return ZX_ERR_BAD_STATE;
  }
  if (perfmon_state) {
    return ZX_ERR_BAD_STATE;
  }

  ktl::unique_ptr<PerfmonState> state;
  auto status = PerfmonState::Create(arch_max_num_cpus(), &state);
  if (status != ZX_OK) {
    return status;
  }

  perfmon_state = ktl::move(state);
  return ZX_OK;
}

zx_status_t arch_perfmon_assign_buffer(uint32_t cpu, fbl::RefPtr<VmObject> vmo) {
  Guard<Mutex> guard(PerfmonLock::Get());

  if (!perfmon_supported) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (atomic_load(&perfmon_active)) {
    return ZX_ERR_BAD_STATE;
  }
  if (!perfmon_state) {
    return ZX_ERR_BAD_STATE;
  }
  if (cpu >= perfmon_state->num_cpus) {
    return ZX_ERR_INVALID_ARGS;
  }

  // A simple safe approximation of the minimum size needed.
  size_t min_size_needed = sizeof(perfmon::BufferHeader);
  min_size_needed += sizeof(perfmon::TimeRecord);
  min_size_needed += perfmon::kMaxNumEvents * kMaxEventRecordSize;
  if (vmo->size() < min_size_needed) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto data = &perfmon_state->cpu_data[cpu];
  data->buffer_vmo = vmo;
  data->buffer_size = vmo->size();
  // The buffer is mapped into kernelspace later.

  return ZX_OK;
}

static zx_status_t arm64_perfmon_verify_fixed_config(const ArchPmuConfig* config,
                                                     unsigned* out_num_used) {
  // There's only one fixed counter on ARM64, the cycle counter.
  PmuEventId id = config->fixed_events[0];
  if (id == perfmon::kEventIdNone) {
    *out_num_used = 0;
    return ZX_OK;
  }

  // The cycle counter which is 64 bits, so no need to validate
  // |config->fixed_initial_value| here.

  // Sanity check on the driver.
  if ((config->fixed_flags[0] & perfmon::kPmuConfigFlagUsesTimebase) &&
      config->timebase_event == perfmon::kEventIdNone) {
    TRACEF("Timebase requested for |fixed_flags[0]|, but not provided\n");
    return ZX_ERR_INVALID_ARGS;
  }

  *out_num_used = 1;
  return ZX_OK;
}

static zx_status_t arm64_perfmon_verify_programmable_config(const ArchPmuConfig* config,
                                                            unsigned* out_num_used) {
  unsigned num_used = perfmon_num_programmable_counters;
  for (unsigned i = 0; i < perfmon_num_programmable_counters; ++i) {
    PmuEventId id = config->programmable_events[i];
    // As a rule this file is agnostic to event ids, it's the device
    // driver's job to map them to the hw values we use. Thus we don't
    // validate the ID here. We are given it so that we can include
    // this ID in the trace output.
    if (id == perfmon::kEventIdNone) {
      num_used = i;
      break;
    }
    if (config->programmable_hw_events[i] & ~ARM64_PMEVTYPERn_EL0_EVCNT_MASK) {
      TRACEF("Extra bits set in |programmable_hw_events[%u]|\n", i);
      return ZX_ERR_INVALID_ARGS;
    }
    // |programmable_initial_value| is 32 bits so no need to validate
    // it here.

    // Sanity check on the driver.
    if ((config->programmable_flags[i] & perfmon::kPmuConfigFlagUsesTimebase) &&
        config->timebase_event == perfmon::kEventIdNone) {
      TRACEF("Timebase requested for |programmable_flags[%u]|, but not provided\n", i);
      return ZX_ERR_INVALID_ARGS;
    }
  }

  *out_num_used = num_used;
  return ZX_OK;
}

static zx_status_t arm64_perfmon_verify_timebase_config(ArchPmuConfig* config, unsigned num_fixed,
                                                        unsigned num_programmable) {
  if (config->timebase_event == perfmon::kEventIdNone) {
    return ZX_OK;
  }

  for (unsigned i = 0; i < num_fixed; ++i) {
    if (config->fixed_events[i] == config->timebase_event) {
      // The PMI code is simpler if this is the case.
      config->fixed_flags[i] &= ~perfmon::kPmuConfigFlagUsesTimebase;
      return ZX_OK;
    }
  }

  for (unsigned i = 0; i < num_programmable; ++i) {
    if (config->programmable_events[i] == config->timebase_event) {
      // The PMI code is simpler if this is the case.
      config->programmable_flags[i] &= ~perfmon::kPmuConfigFlagUsesTimebase;
      return ZX_OK;
    }
  }

  TRACEF("Timebase 0x%x requested but not present\n", config->timebase_event);
  return ZX_ERR_INVALID_ARGS;
}

static zx_status_t arm64_perfmon_verify_config(ArchPmuConfig* config, PerfmonState* state) {
  // It's the driver's job to verify user provided parameters.
  // Our only job is to verify that what the driver gives us makes sense
  // and that we won't crash.
  unsigned num_used_fixed;
  auto status = arm64_perfmon_verify_fixed_config(config, &num_used_fixed);
  if (status != ZX_OK) {
    return status;
  }
  state->num_used_fixed = num_used_fixed;

  unsigned num_used_programmable;
  status = arm64_perfmon_verify_programmable_config(config, &num_used_programmable);
  if (status != ZX_OK) {
    return status;
  }
  state->num_used_programmable = num_used_programmable;

  status = arm64_perfmon_verify_timebase_config(config, state->num_used_fixed,
                                                state->num_used_programmable);
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

static void arm64_perfmon_stage_fixed_config(const ArchPmuConfig* config, PerfmonState* state) {
  static_assert(sizeof(state->fixed_events) == sizeof(config->fixed_events), "");
  memcpy(state->fixed_events, config->fixed_events, sizeof(state->fixed_events));

  static_assert(sizeof(state->fixed_initial_value) == sizeof(config->fixed_initial_value), "");
  memcpy(state->fixed_initial_value, config->fixed_initial_value,
         sizeof(state->fixed_initial_value));

  static_assert(sizeof(state->fixed_flags) == sizeof(config->fixed_flags), "");
  memcpy(state->fixed_flags, config->fixed_flags, sizeof(state->fixed_flags));

  memset(state->fixed_hw_events, 0, sizeof(state->fixed_hw_events));

  if (state->num_used_fixed > 0) {
    DEBUG_ASSERT(state->num_used_fixed == 1);
    DEBUG_ASSERT(state->fixed_events[0] != perfmon::kEventIdNone);
    // Don't generate PMI's for counters that use another as the timebase.
    // We still generate interrupts in "counting mode" in case the counter
    // overflows.
    if (!(config->fixed_flags[0] & perfmon::kPmuConfigFlagUsesTimebase)) {
      state->pmintenset_el1 |= ARM64_PMINTENSET_EL1_C_MASK;
    }
    state->pm_counter_ctrl |= ARM64_PMOVSCLR_EL0_C_MASK;
    uint32_t ctrl = 0;
    // We leave the NSK,NSU bits as zero here, which translates as
    // non-secure EL0,EL1 modes being treated same as secure modes.
    // TODO(dje): Review.
    if (!(config->fixed_flags[0] & perfmon::kPmuConfigFlagOs)) {
      ctrl |= ARM64_PMCCFILTR_EL0_P_MASK;
    }
    if (!(config->fixed_flags[0] & perfmon::kPmuConfigFlagUser)) {
      ctrl |= ARM64_PMCCFILTR_EL0_U_MASK;
    }
    state->fixed_hw_events[0] |= ctrl;
  }
}

static void arm64_perfmon_stage_programmable_config(const ArchPmuConfig* config,
                                                    PerfmonState* state) {
  static_assert(sizeof(state->programmable_events) == sizeof(config->programmable_events), "");
  memcpy(state->programmable_events, config->programmable_events,
         sizeof(state->programmable_events));

  static_assert(
      sizeof(state->programmable_initial_value) == sizeof(config->programmable_initial_value), "");
  memcpy(state->programmable_initial_value, config->programmable_initial_value,
         sizeof(state->programmable_initial_value));

  static_assert(sizeof(state->programmable_flags) == sizeof(config->programmable_flags), "");
  memcpy(state->programmable_flags, config->programmable_flags, sizeof(state->programmable_flags));

  memset(state->programmable_hw_events, 0, sizeof(state->programmable_hw_events));

  for (unsigned i = 0; i < state->num_used_programmable; ++i) {
    // Don't generate PMI's for counters that use another as the timebase.
    // We still generate interrupts in "counting mode" in case the counter
    // overflows.
    if (!(config->programmable_flags[i] & perfmon::kPmuConfigFlagUsesTimebase)) {
      state->pmintenset_el1 |= ARM64_PMU_PROGRAMMABLE_COUNTER_MASK(i);
    }
    state->pm_counter_ctrl |= ARM64_PMU_PROGRAMMABLE_COUNTER_MASK(i);
    uint32_t ctrl = 0;
    // We leave the NSK,NSU bits as zero here, which translates as
    // non-secure EL0,EL1 modes being treated same as secure modes.
    // TODO(dje): Review.
    if (!(config->programmable_flags[i] & perfmon::kPmuConfigFlagOs)) {
      ctrl |= ARM64_PMEVTYPERn_EL0_P_MASK;
    }
    if (!(config->programmable_flags[i] & perfmon::kPmuConfigFlagUser)) {
      ctrl |= ARM64_PMEVTYPERn_EL0_U_MASK;
    }
    // TODO(dje): MT bit
    state->programmable_hw_events[i] = config->programmable_hw_events[i] | ctrl;
  }
}

// Stage the configuration for later activation by START.
// One of the main goals of this function is to verify the provided config
// is ok, e.g., it won't cause us to crash.
zx_status_t arch_perfmon_stage_config(ArchPmuConfig* config) {
  Guard<Mutex> guard(PerfmonLock::Get());

  if (!perfmon_supported) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (atomic_load(&perfmon_active)) {
    return ZX_ERR_BAD_STATE;
  }
  if (!perfmon_state) {
    return ZX_ERR_BAD_STATE;
  }

  auto state = perfmon_state.get();

  // Note: The verification pass may also alter |config| to make things
  // simpler for the implementation.
  auto status = arm64_perfmon_verify_config(config, state);
  if (status != ZX_OK) {
    return status;
  }

  state->timebase_event = config->timebase_event;

  arm64_perfmon_stage_fixed_config(config, state);
  arm64_perfmon_stage_programmable_config(config, state);

  // Enable the perf counters:
  // E = Enable bit
  // LC = Record cycle counter overflows
  // Noteworthy bits that are not set:
  // D = clock divider, 0 = PMCCNTR_EL0 counts every cycle
  // C = reset cycle counter to zero
  // P = reset event counters (other than cycle counter) to zero
  // The counters are not reset because their values are decided elsewhere.
  state->pmcr_el0 = ARM64_PMCR_EL0_E_MASK | ARM64_PMCR_EL0_LC_MASK;

  return ZX_OK;
}

static void arm64_perfmon_unmap_buffers_locked(PerfmonState* state) {
  unsigned num_cpus = state->num_cpus;
  for (unsigned cpu = 0; cpu < num_cpus; ++cpu) {
    auto data = &state->cpu_data[cpu];
    if (data->buffer_start) {
      data->buffer_mapping->Destroy();
    }
    data->buffer_mapping.reset();
    data->buffer_start = nullptr;
    data->buffer_end = nullptr;
    data->buffer_next = nullptr;
  }
  LTRACEF("buffers unmapped\n");
}

static zx_status_t arm64_perfmon_map_buffers_locked(PerfmonState* state) {
  zx_status_t status = ZX_OK;
  unsigned num_cpus = state->num_cpus;
  for (unsigned cpu = 0; cpu < num_cpus; ++cpu) {
    auto data = &state->cpu_data[cpu];
    // Heads up: The logic is off if |vmo_offset| is non-zero.
    const uint64_t vmo_offset = 0;
    const size_t size = data->buffer_size;
    const uint arch_mmu_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;
    const char* name = "pmu-buffer";
    status = VmAspace::kernel_aspace()->RootVmar()->CreateVmMapping(
        0 /* ignored */, size, 0 /* align pow2 */, 0 /* vmar flags */, data->buffer_vmo, vmo_offset,
        arch_mmu_flags, name, &data->buffer_mapping);
    if (status != ZX_OK) {
      TRACEF("error %d mapping buffer: cpu %u, size 0x%zx\n", status, cpu, size);
      break;
    }
    // Pass true for |commit| so that we get our pages mapped up front.
    // Otherwise we'll need to allow for a page fault to happen in the
    // PMI handler.
    status = data->buffer_mapping->MapRange(vmo_offset, size, true);
    if (status != ZX_OK) {
      TRACEF("error %d mapping range: cpu %u, size 0x%zx\n", status, cpu, size);
      data->buffer_mapping->Destroy();
      data->buffer_mapping.reset();
      break;
    }
    data->buffer_start =
        reinterpret_cast<perfmon::BufferHeader*>(data->buffer_mapping->base() + vmo_offset);
    data->buffer_end = reinterpret_cast<char*>(data->buffer_start) + size;
    LTRACEF("buffer mapped: cpu %u, start %p, end %p\n", cpu, data->buffer_start, data->buffer_end);

    auto hdr = data->buffer_start;
    hdr->version = perfmon::kBufferVersion;
    hdr->arch = perfmon::kArchArm64;
    hdr->flags = 0;
    hdr->ticks_per_second = ticks_per_second();
    hdr->capture_end = sizeof(*hdr);
    data->buffer_next = reinterpret_cast<perfmon::RecordHeader*>(
        reinterpret_cast<char*>(data->buffer_start) + hdr->capture_end);
  }

  if (status != ZX_OK) {
    arm64_perfmon_unmap_buffers_locked(state);
  }

  return status;
}

// This is invoked via mp_sync_exec which thread safety analysis cannot follow.
static void arm64_perfmon_start_task(void* raw_context) TA_NO_THREAD_SAFETY_ANALYSIS {
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(!atomic_load(&perfmon_active) && raw_context);

  auto state = reinterpret_cast<PerfmonState*>(raw_context);

  if (state->num_used_fixed > 0) {
    DEBUG_ASSERT(state->num_used_fixed == 1);
    __arm_wsr64("pmccfiltr_el0", state->fixed_hw_events[0]);
    __arm_wsr64("pmccntr_el0", state->fixed_initial_value[0]);
  }

  for (unsigned i = 0; i < state->num_used_programmable; ++i) {
    __arm_wsr64("pmselr_el0", i);
    __arm_wsr64("pmxevtyper_el0", state->programmable_hw_events[i]);
    __arm_wsr64("pmxevcntr_el0", state->programmable_initial_value[i]);
  }

  __arm_wsr64("pmcntenset_el0", state->pm_counter_ctrl);
  __arm_wsr64("pmintenset_el1", state->pmintenset_el1);

  // TODO(fxbug.dev/33106): arm64_pmu_enable_our_irq(true); - needs irq support

  // Enable counters as late as possible so that our setup doesn't contribute
  // to the data.
  enable_counters(state);
}

// Begin collecting data.

zx_status_t arch_perfmon_start() {
  Guard<Mutex> guard(PerfmonLock::Get());

  if (!perfmon_supported) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (atomic_load(&perfmon_active)) {
    return ZX_ERR_BAD_STATE;
  }
  if (!perfmon_state) {
    return ZX_ERR_BAD_STATE;
  }

  // Make sure all relevant sysregs have been wiped clean.
  if (!perfmon_hw_initialized) {
    mp_sync_exec(MP_IPI_TARGET_ALL, 0, arm64_perfmon_reset_task, nullptr);
    perfmon_hw_initialized = true;
  }

  // Sanity check the buffers and map them in.
  // This is deferred until now so that they are mapped in as minimally as
  // necessary.
  // TODO(dje): OTOH one might want to start/stop/start/stop/... and
  // continually mapping/unmapping will be painful. Revisit when things
  // settle down.
  auto state = perfmon_state.get();
  auto status = arm64_perfmon_map_buffers_locked(state);
  if (status != ZX_OK) {
    return status;
  }

  TRACEF("Enabling perfmon, %u fixed, %u programmable\n", state->num_used_fixed,
         state->num_used_programmable);
  if (LOCAL_TRACE) {
    LTRACEF("pmcr: 0x%x\n", state->pmcr_el0);
    for (unsigned i = 0; i < state->num_used_fixed; ++i) {
      LTRACEF("fixed[%u]: type 0x%x, initial 0x%lx\n", i, state->fixed_hw_events[i],
              state->fixed_initial_value[i]);
    }
    for (unsigned i = 0; i < state->num_used_programmable; ++i) {
      LTRACEF("programmable[%u]: id 0x%x, type 0x%x, initial 0x%x\n", i,
              state->programmable_events[i], state->programmable_hw_events[i],
              state->programmable_initial_value[i]);
    }
  }

  mp_sync_exec(MP_IPI_TARGET_ALL, 0, arm64_perfmon_start_task, state);
  atomic_store(&perfmon_active, true);

  return ZX_OK;
}

// This is invoked via mp_sync_exec which thread safety analysis cannot follow.
static void arm64_perfmon_write_last_records(PerfmonState* state,
                                             uint32_t cpu) TA_NO_THREAD_SAFETY_ANALYSIS {
  PerfmonCpuData* data = &state->cpu_data[cpu];
  perfmon::RecordHeader* next = data->buffer_next;

  zx_ticks_t now = current_ticks();
  next = arch_perfmon_write_time_record(next, perfmon::kEventIdNone, now);

  // If the counter triggers interrupts then the PMI handler will
  // continually reset it to its initial value. To keep things simple
  // just always subtract out the initial value from the current value
  // and write the difference out. For non-interrupt triggering events
  // the user should normally initialize the counter to zero to get
  // correct results.
  // Counters that don't trigger interrupts could overflow and we won't
  // necessarily catch it, but there's nothing we can do about it.
  // We can handle the overflowed-once case, which should catch the
  // vast majority of cases.

  for (unsigned i = 0; i < state->num_used_programmable; ++i) {
    PmuEventId id = state->programmable_events[i];
    DEBUG_ASSERT(id != 0);
    __arm_wsr64("pmselr_el0", i);
    uint64_t count = __arm_rsr64("pmxevcntr_el0");
    if (count >= state->programmable_initial_value[i]) {
      count -= state->programmable_initial_value[i];
    } else {
      count += (kMaxProgrammableCounterValue - state->programmable_initial_value[i] + 1);
    }
    next = arch_perfmon_write_count_record(next, id, count);
  }

  // There is only one fixed counter, the cycle counter.
  if (state->num_used_fixed > 0) {
    DEBUG_ASSERT(state->num_used_fixed == 1);
    PmuEventId id = state->fixed_events[0];
    uint64_t count = __arm_rsr64("pmccntr_el0");
    if (count >= state->fixed_initial_value[0]) {
      count -= state->fixed_initial_value[0];
    } else {
      count += (kMaxFixedCounterValue - state->fixed_initial_value[0] + 1);
    }
    next = arch_perfmon_write_count_record(next, id, count);
  }

  data->buffer_next = next;
}

// This is invoked via mp_sync_exec which thread safety analysis cannot follow.
static void arm64_perfmon_finalize_buffer(PerfmonState* state,
                                          uint32_t cpu) TA_NO_THREAD_SAFETY_ANALYSIS {
  TRACEF("Collecting last data for cpu %u\n", cpu);

  PerfmonCpuData* data = &state->cpu_data[cpu];
  perfmon::BufferHeader* hdr = data->buffer_start;

  // KISS. There may be enough space to write some of what we want to write
  // here, but don't try. Just use the same simple check that
  // |pmi_interrupt_handler()| does.
  size_t space_needed = get_max_space_needed_for_all_records(state);
  if (reinterpret_cast<char*>(data->buffer_next) + space_needed > data->buffer_end) {
    hdr->flags |= perfmon::BufferHeader::kBufferFlagFull;
    LTRACEF("Buffer overflow on cpu %u\n", cpu);
  } else {
    arm64_perfmon_write_last_records(state, cpu);
  }

  hdr->capture_end =
      reinterpret_cast<char*>(data->buffer_next) - reinterpret_cast<char*>(data->buffer_start);
}

// This is invoked via mp_sync_exec which thread safety analysis cannot follow.
static void arm64_perfmon_stop_task(void* raw_context) TA_NO_THREAD_SAFETY_ANALYSIS {
  // Disable all counters ASAP.
  disable_counters();
  // TODO(fxbug.dev/33106): arm64_pmu_enable_our_irq(false); - needs irq support

  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(!atomic_load(&perfmon_active));
  DEBUG_ASSERT(raw_context);

  auto state = reinterpret_cast<PerfmonState*>(raw_context);
  auto cpu = arch_curr_cpu_num();
  auto data = &state->cpu_data[cpu];

  // Retrieve final event values and write into the trace buffer.

  if (data->buffer_start) {
    arm64_perfmon_finalize_buffer(state, cpu);
  }

  arm64_perfmon_clear_overflow_indicators();
}

void arch_perfmon_stop_locked() TA_REQ(PerfmonLock::Get()) {
  if (!perfmon_supported) {
    // Nothing to do.
    return;
  }
  if (!perfmon_state) {
    // Nothing to do.
    return;
  }
  if (!atomic_load(&perfmon_active)) {
    // Nothing to do.
    return;
  }

  TRACEF("Disabling perfmon\n");

  // Do this before anything else so that any PMI interrupts from this point
  // on won't try to access potentially unmapped memory.
  atomic_store(&perfmon_active, false);

  // TODO(dje): Check clobbering of values - user should be able to do
  // multiple stops and still read register values.

  auto state = perfmon_state.get();
  mp_sync_exec(MP_IPI_TARGET_ALL, 0, arm64_perfmon_stop_task, state);

  // arm64_perfmon_start currently maps the buffers in, so we unmap them here.
  // Make sure to do this after we've turned everything off so that we
  // don't get another PMI after this.
  arm64_perfmon_unmap_buffers_locked(state);
}

// Stop collecting data.
void arch_perfmon_stop() {
  Guard<Mutex> guard(PerfmonLock::Get());
  arch_perfmon_stop_locked();
}

// Worker for arm64_perfmon_fini to be executed on all cpus.
// This is invoked via mp_sync_exec which thread safety analysis cannot follow.
static void arm64_perfmon_reset_task(void* raw_context) TA_NO_THREAD_SAFETY_ANALYSIS {
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(!atomic_load(&perfmon_active));
  DEBUG_ASSERT(!raw_context);

  // Disable everything.
  // Also, reset the counters, don't leave old values lying around.
  uint32_t pmcr = ARM64_PMCR_EL0_P_MASK | ARM64_PMCR_EL0_C_MASK;
  __arm_wsr64("pmcr_el0", pmcr);
  // TODO(fxbug.dev/33106): arm64_pmu_enable_our_irq(false); - needs irq support
  arm64_perfmon_clear_overflow_indicators();

  __arm_wsr64("pmcntenclr_el0", ~0u);
  __arm_wsr64("pmintenclr_el1", ~0u);
  __arm_wsr64("pmccfiltr_el0", 0);
  for (unsigned i = 0; i < perfmon_num_programmable_counters; ++i) {
    // This isn't performance sensitive, so KISS and go through pmselr.
    __arm_wsr64("pmselr_el0", i);
    __arm_wsr64("pmxevtyper_el0", 0);
  }
}

// Finish data collection, reset h/w back to initial state and undo
// everything arm64_perfmon_init did.
void arch_perfmon_fini() {
  Guard<Mutex> guard(PerfmonLock::Get());

  if (!perfmon_supported) {
    // Nothing to do.
    return;
  }

  if (atomic_load(&perfmon_active)) {
    arch_perfmon_stop_locked();
    DEBUG_ASSERT(!atomic_load(&perfmon_active));
  }

  mp_sync_exec(MP_IPI_TARGET_ALL, 0, arm64_perfmon_reset_task, nullptr);

  perfmon_state.reset();
}

// Interrupt handling.

// Helper function so that there is only one place where we enable/disable
// interrupts (our caller).
// Returns true if success, false if buffer is full.

static bool pmi_interrupt_handler(const iframe_t* frame, PerfmonState* state) {
  cpu_num_t cpu = arch_curr_cpu_num();
  auto data = &state->cpu_data[cpu];

  zx_ticks_t now = current_ticks();
  TRACEF("cpu %u: now %ld, sp %p\n", cpu, now, __GET_FRAME());

  // Rather than continually checking if we have enough space, just
  // conservatively check for the maximum amount we'll need.
  size_t space_needed = get_max_space_needed_for_all_records(state);
  if (reinterpret_cast<char*>(data->buffer_next) + space_needed > data->buffer_end) {
    TRACEF("cpu %u: @%ld pmi buffer full\n", cpu, now);
    data->buffer_start->flags |= perfmon::BufferHeader::kBufferFlagFull;
    return false;
  }

  const uint32_t status = __arm_rsr("pmovsset_el0");
  uint32_t bits_to_clear = 0;
  uint64_t aspace = __arm_rsr64("ttbr0_el1");

  LTRACEF("cpu %u: status 0x%x\n", cpu, status);

  if (status & perfmon_counter_status_bits) {
    auto next = data->buffer_next;
    bool saw_timebase = false;

    next = arch_perfmon_write_time_record(next, perfmon::kEventIdNone, now);

    // Note: We don't write "value" records here instead prefering the
    // smaller "tick" record. If the user is tallying the counts the user
    // is required to recognize this and apply the tick rate.
    // TODO(dje): Precompute mask to detect whether the interrupt is for
    // the timebase counter, and then combine the loops.

    for (unsigned i = 0; i < state->num_used_programmable; ++i) {
      if (!(status & ARM64_PMU_PROGRAMMABLE_COUNTER_MASK(i))) {
        continue;
      }
      PmuEventId id = state->programmable_events[i];
      // Counters using a separate timebase are handled below.
      // We shouldn't get an interrupt on a counter using a timebase.
      // TODO(dje): The counter could still overflow. Later.
      if (id == state->timebase_event) {
        saw_timebase = true;
      } else if (state->programmable_flags[i] & perfmon::kPmuConfigFlagUsesTimebase) {
        continue;
      }
      // TODO(dje): Counter still counting.
      if (state->programmable_flags[i] & perfmon::kPmuConfigFlagPc) {
        next = arch_perfmon_write_pc_record(next, id, aspace, frame->elr);
      } else {
        next = arch_perfmon_write_tick_record(next, id);
      }
      LTRACEF("cpu %u: resetting PMC %u to 0x%x\n", cpu, i, state->programmable_initial_value[i]);
      __arm_wsr64("pmselr_el0", i);
      __arm_wsr64("pmxevcntr_el0", state->programmable_initial_value[i]);
    }

    for (unsigned i = 0; i < state->num_used_fixed; ++i) {
      DEBUG_ASSERT(state->num_used_fixed == 1);
      if (!(status & ARM64_PMOVSSET_EL0_C_MASK)) {
        continue;
      }
      PmuEventId id = state->fixed_events[0];
      // Counters using a separate timebase are handled below.
      // We shouldn't get an interrupt on a counter using a timebase.
      // TODO(dje): The counter could still overflow. Later.
      if (id == state->timebase_event) {
        saw_timebase = true;
      } else if (state->fixed_flags[0] & perfmon::kPmuConfigFlagUsesTimebase) {
        continue;
      }
      // TODO(dje): Counter still counting.
      if (state->fixed_flags[0] & perfmon::kPmuConfigFlagPc) {
        next = arch_perfmon_write_pc_record(next, id, aspace, frame->elr);
      } else {
        next = arch_perfmon_write_tick_record(next, id);
      }
      LTRACEF("cpu %u: resetting cycle counter to 0x%lx\n", cpu, state->fixed_initial_value[0]);
      __arm_wsr64("pmccntr_el0", state->fixed_initial_value[0]);
    }

    // Now handle events that have perfmon::kPmuConfigFlagTimebase0 set.
    if (saw_timebase) {
      for (unsigned i = 0; i < state->num_used_programmable; ++i) {
        if (!(state->programmable_flags[i] & perfmon::kPmuConfigFlagUsesTimebase)) {
          continue;
        }
        PmuEventId id = state->programmable_events[i];
        __arm_wsr64("pmselr_el0", i);
        uint64_t count = __arm_rsr64("pmxevcntr_el0");
        next = arch_perfmon_write_count_record(next, id, count);
        // We could leave the counter alone, but it could overflow.
        // Instead reduce the risk and just always reset to zero.
        LTRACEF("cpu %u: resetting PMC %u to 0x%x\n", cpu, i, state->programmable_initial_value[i]);
        // Note: This used the value of |pmselr_el0| set above.
        __arm_wsr64("pmxevcntr_el0", state->programmable_initial_value[i]);
      }
      for (unsigned i = 0; i < state->num_used_fixed; ++i) {
        DEBUG_ASSERT(state->num_used_fixed == 1);
        if (!(state->fixed_flags[0] & perfmon::kPmuConfigFlagUsesTimebase)) {
          continue;
        }
        PmuEventId id = state->fixed_events[0];
        uint64_t count = __arm_rsr64("pmccntr_el0");
        next = arch_perfmon_write_count_record(next, id, count);
        // We could leave the counter alone, but it could overflow.
        // Instead reduce the risk and just always reset to zero.
        LTRACEF("cpu %u: resetting cycle counter to 0x%lx\n", cpu, state->fixed_initial_value[0]);
        __arm_wsr64("pmccntr_el0", state->fixed_initial_value[0]);
      }
    }

    data->buffer_next = next;
  }

  bits_to_clear |= perfmon_counter_status_bits;

  LTRACEF("cpu %u: clearing status bits 0x%x\n", cpu, bits_to_clear);
  __arm_wsr64("pmovsclr_el0", bits_to_clear);

  return true;
}

void arm64_pmi_interrupt_handler(const iframe_t* frame) TA_NO_THREAD_SAFETY_ANALYSIS {
  if (!atomic_load(&perfmon_active)) {
    return;
  }

  // Turn all counters off as soon as possible so that the counters that
  // haven't overflowed yet stop counting while we're working.
  disable_counters();

  DEBUG_ASSERT(arch_ints_disabled());

  CPU_STATS_INC(perf_ints);

  auto state = perfmon_state.get();

#if 0
  // TODO(dje): We may want this anyway. If we want to be able to handle
  // page faults inside this handler we'll need to turn interrupts back
  // on. At the moment we can't do this as we don't handle recursive PMIs.
  arch_set_blocking_disallowed(false);
  arch_enable_ints();
#endif

  bool success = pmi_interrupt_handler(frame, state);

#if 0
  arch_disable_ints();
  arch_set_blocking_disallowed(true);
#endif

  // If buffer is full leave everything turned off.
  if (!success) {
    // Don't restore PMCR_EL0, leave everything turned off.
  } else {
    // This is the last thing we do: Once we do this the counters
    // will start counting again.
    enable_counters(state);
  }
}
