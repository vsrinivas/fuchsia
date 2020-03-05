// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>

#include <arch/arch_perfmon.h>
#include <fbl/ref_ptr.h>
#include <kernel/align.h>
#include <kernel/mp.h>
#include <kernel/mutex.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>

using PmuEventId = perfmon::EventId;

// While the last-branch record is far larger, it is not emitted for each
// event.
static constexpr size_t kMaxEventRecordSize = sizeof(perfmon::PcRecord);

struct PerfmonCpuData {
  // The trace buffer, passed in from userspace.
  fbl::RefPtr<VmObject> buffer_vmo;
  size_t buffer_size = 0;

  // The trace buffer when mapped into kernel space.
  // This is only done while the trace is running.
  fbl::RefPtr<VmMapping> buffer_mapping;
  perfmon::BufferHeader* buffer_start = 0;
  void* buffer_end = 0;

  // The next record to fill.
  perfmon::RecordHeader* buffer_next = nullptr;
} __CPU_ALIGN;

struct PerfmonStateBase {
  explicit PerfmonStateBase(unsigned n_cpus);
  ~PerfmonStateBase();

  // Helper to allocate space for per-cpu state.
  // This data must be properly aligned so it is allocated specially.
  bool AllocatePerCpuData();

  // Number of entries in |cpu_data|.
  const unsigned num_cpus;

  // An array with one entry for each cpu.
  // TODO(dje): Ideally this would be something like
  // ktl::unique_ptr<PerfmonCpuData[]> cpu_data;
  // but that will need to wait for a "new" that handles aligned allocs.
  PerfmonCpuData* cpu_data = nullptr;
};

// True if the chip supports perfmon at the version we require.
extern bool perfmon_supported;

// This is accessed atomically as it is also accessed by the PMI handler.
extern int perfmon_active;

// The functions performing |mtrace_control()| operations.

// Perform MTRACE_PERFMON_GET_PROPERTIES: Store PMU properties in |state|.
zx_status_t arch_perfmon_get_properties(ArchPmuProperties* state);

// Perform MTRACE_PERFMON_INIT: Prepare for a data collection run.
zx_status_t arch_perfmon_init();

// Perform MTRACE_PERFMON_ASSIGN_BUFFER: Assign |vmo| as the buffer for |cpu|.
// The VMO is not mapped into kernel space yet, that is done later by
// |arch_perfmon_start()|.
zx_status_t arch_perfmon_assign_buffer(uint32_t cpu, fbl::RefPtr<VmObject> vmo);

// Perform MTRACE_PERFMON_STAGE_CONFIG: Record |config| as the configuration
// of data to be collected.
zx_status_t arch_perfmon_stage_config(ArchPmuConfig* config);

// Perform MTRACE_PERFMON_START: Initialize PMU registers, map VMOs into
// kernel space, and turn on PMU interrupt if necessary.
zx_status_t arch_perfmon_start();

// Perform MTRACE_PERFMON_STOP: Stop data collection, including collecting
// the final values of the counters and unmapping the VMOs.
// It's ok to call this multiple times.
void arch_perfmon_stop();

// Perform MTRACE_PERFMON_FINI: Terminate data collection, reset all PMU
// registers. Data collection is stopped first if necessary.
// It's ok to call this multiple times.
void arch_perfmon_fini();

// This section contains helper routines to write perfmon records.

static inline void arch_perfmon_write_header(perfmon::RecordHeader* hdr, perfmon::RecordType type,
                                             PmuEventId event) {
  hdr->type = type;
  hdr->reserved_flags = 0;
  hdr->event = event;
}

static inline perfmon::RecordHeader* arch_perfmon_write_time_record(perfmon::RecordHeader* hdr,
                                                                    PmuEventId event,
                                                                    zx_ticks_t time) {
  auto rec = reinterpret_cast<perfmon::TimeRecord*>(hdr);
  arch_perfmon_write_header(&rec->header, perfmon::kRecordTypeTime, event);
  rec->time = time;
  ++rec;
  return reinterpret_cast<perfmon::RecordHeader*>(rec);
}

static inline perfmon::RecordHeader* arch_perfmon_write_tick_record(perfmon::RecordHeader* hdr,
                                                                    PmuEventId event) {
  auto rec = reinterpret_cast<perfmon::TickRecord*>(hdr);
  arch_perfmon_write_header(&rec->header, perfmon::kRecordTypeTick, event);
  ++rec;
  return reinterpret_cast<perfmon::RecordHeader*>(rec);
}

static inline perfmon::RecordHeader* arch_perfmon_write_count_record(perfmon::RecordHeader* hdr,
                                                                     PmuEventId event,
                                                                     uint64_t count) {
  auto rec = reinterpret_cast<perfmon::CountRecord*>(hdr);
  arch_perfmon_write_header(&rec->header, perfmon::kRecordTypeCount, event);
  rec->count = count;
  ++rec;
  return reinterpret_cast<perfmon::RecordHeader*>(rec);
}

static inline perfmon::RecordHeader* arch_perfmon_write_value_record(perfmon::RecordHeader* hdr,
                                                                     PmuEventId event,
                                                                     uint64_t value) {
  auto rec = reinterpret_cast<perfmon::ValueRecord*>(hdr);
  arch_perfmon_write_header(&rec->header, perfmon::kRecordTypeValue, event);
  rec->value = value;
  ++rec;
  return reinterpret_cast<perfmon::RecordHeader*>(rec);
}

static inline perfmon::RecordHeader* arch_perfmon_write_pc_record(perfmon::RecordHeader* hdr,
                                                                  PmuEventId event, uint64_t aspace,
                                                                  uint64_t pc) {
  auto rec = reinterpret_cast<perfmon::PcRecord*>(hdr);
  arch_perfmon_write_header(&rec->header, perfmon::kRecordTypePc, event);
  rec->aspace = aspace;
  rec->pc = pc;
  ++rec;
  return reinterpret_cast<perfmon::RecordHeader*>(rec);
}
