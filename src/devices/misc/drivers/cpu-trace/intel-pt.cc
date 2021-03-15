// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// See the README.md in this directory for documentation.

#include <assert.h>
#include <cpuid.h>
#include <fuchsia/hardware/cpu/insntrace/llcpp/fidl.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <inttypes.h>
#include <lib/ddk/platform-defs.h>
#include <lib/zircon-internal/device/cpu-trace/intel-pt.h>
#include <lib/zircon-internal/mtrace.h>
#include <lib/zx/bti.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/resource.h>
#include <zircon/types.h>

#include <memory>
#include <new>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/io-buffer.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/alloc_checker.h>

namespace insntrace {

namespace fuchsia_insntrace = fuchsia_hardware_cpu_insntrace;

// Shorten some long fidl names.
using BufferConfig = fuchsia_insntrace::wire::BufferConfig;
using BufferState = fuchsia_insntrace::wire::BufferState;

// This is defined in insntrace.fidl but not emitted.
using BufferDescriptor = uint32_t;

typedef struct ipt_per_trace_state {
  // the cpu or thread this buffer is assigned to
  // Which value to use is determined by the trace mode.
  union {
    uint32_t cpu;
    zx_handle_t thread;
  } owner;

  // number of chunks, each 2^|chunk_order| pages in size
  uint32_t num_chunks;
  // log2 size of each chunk, in pages
  uint32_t chunk_order;
  // if true then the buffer is circular, otherwise tracing stops when the
  // buffer fills
  bool is_circular;
  // true if allocated
  bool allocated;
  // true if buffer is assigned to a cpu/thread
  bool assigned;
  // number of ToPA tables needed
  uint32_t num_tables;

  // msrs
  uint64_t ctl;
  uint64_t status;
  uint64_t output_base;
  uint64_t output_mask_ptrs;
  uint64_t cr3_match;
  struct {
    uint64_t a, b;
  } addr_ranges[IPT_MAX_NUM_ADDR_RANGES];

  // trace buffers and ToPA tables
  // ToPA: Table of Physical Addresses
  // A "trace buffer" is a set of N chunks.
  std::unique_ptr<io_buffer_t[]> chunks;
  std::unique_ptr<io_buffer_t[]> topas;
} ipt_per_trace_state_t;

// TODO(dje): add unbindable?
class InsntraceDevice;
using DeviceType = ddk::Device<InsntraceDevice, ddk::Openable, ddk::Closable, ddk::Messageable>;

class InsntraceDevice : public DeviceType, fuchsia_insntrace::Controller::Interface {
 public:
  explicit InsntraceDevice(zx_device_t* parent, zx::bti bti)
      : DeviceType(parent), bti_(std::move(bti)) {}
  ~InsntraceDevice() = default;

  void DdkRelease();

  // Fidl handlers
  zx_status_t IptInitialize(const fuchsia_insntrace::wire::Allocation* allocation);
  zx_status_t IptTerminate();
  zx_status_t IptGetAllocation(fuchsia_insntrace::wire::Allocation* config);
  zx_status_t IptAllocateBuffer(const BufferConfig* config, BufferDescriptor* out_descriptor);
  zx_status_t IptAssignThreadBuffer(BufferDescriptor descriptor, zx_handle_t thread);
  zx_status_t IptReleaseThreadBuffer(BufferDescriptor descriptor, zx_handle_t thread);
  zx_status_t IptGetBufferConfig(BufferDescriptor descriptor, BufferConfig* out_config);
  zx_status_t IptGetBufferState(BufferDescriptor descriptor, BufferState* out_state);
  zx_status_t IptGetChunkHandle(BufferDescriptor descriptor, uint32_t chunk_num,
                                zx_handle_t* out_handle);
  zx_status_t IptFreeBuffer(BufferDescriptor descriptor);
  zx_status_t IptStart();
  zx_status_t IptStop();

  // Fidl server interface implementation
  void Initialize(fuchsia_insntrace::wire::Allocation allocation,
                  InitializeCompleter::Sync& completer) override;
  void Terminate(TerminateCompleter::Sync& completer) override;
  void GetAllocation(GetAllocationCompleter::Sync& completer) override;
  void AllocateBuffer(BufferConfig config, AllocateBufferCompleter::Sync& completer) override;
  void AssignThreadBuffer(BufferDescriptor descriptor, zx::thread thread,
                          AssignThreadBufferCompleter::Sync& completer) override;
  void ReleaseThreadBuffer(BufferDescriptor descriptor, zx::thread thread,
                           ReleaseThreadBufferCompleter::Sync& completer) override;
  void GetBufferConfig(BufferDescriptor descriptor,
                       GetBufferConfigCompleter::Sync& completer) override;
  void GetBufferState(BufferDescriptor descriptor,
                      GetBufferStateCompleter::Sync& completer) override;
  void GetChunkHandle(BufferDescriptor descriptor, uint32_t chunk_num,
                      GetChunkHandleCompleter::Sync& completer) override;
  void FreeBuffer(BufferDescriptor descriptor, FreeBufferCompleter::Sync& completer) override;
  void Start(StartCompleter::Sync& completer) override;
  void Stop(StopCompleter::Sync& completer) override;

  // Device protocol implementation
  zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags);
  zx_status_t DdkClose(uint32_t flags);
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);

 private:
  // Low level routines
  void MakeTopa(ipt_per_trace_state_t* per_trace);
  uint32_t ComputeTopaEntryCount(ipt_per_trace_state_t* per_trace);
  size_t ComputeCaptureSize(const ipt_per_trace_state_t* per_trace);
  zx_status_t X86PtAllocBuffer1(ipt_per_trace_state_t* per_trace, uint32_t num, uint32_t order,
                                bool is_circular);
  void X86PtFreeBuffer1(ipt_per_trace_state_t* per_trace);
  zx_status_t X86PtAllocBuffer(const BufferConfig* config, BufferDescriptor* out_descriptor);
  zx_status_t X86PtAssignThreadBuffer(BufferDescriptor descriptor, zx_handle_t thread);
  zx_status_t X86PtReleaseThreadBuffer(BufferDescriptor descriptor, zx_handle_t thread);
  zx_status_t X86PtFreeBuffer(BufferDescriptor descriptor);
  zx_status_t X86PtStageTraceData(zx_handle_t resource, BufferDescriptor descriptor);
  zx_status_t X86PtGetTraceData(zx_handle_t resource, BufferDescriptor descriptor);

  mtx_t lock_{};

  // Only one open of this device is supported at a time. KISS for now.
  bool opened_ = false;

  // Once tracing has started various things are not allowed until it stops.
  bool active_ = false;

  zx_insntrace_trace_mode_t mode_ = IPT_MODE_CPU;

  // # of entries in |per_trace_state|.
  // When tracing by cpu, this is the max number of cpus.
  // When tracing by thread, this is the max number of threads.
  // TODO(dje): Add support for dynamically growing the vector.
  uint16_t num_traces_ = 0;

  // one entry for each trace
  std::unique_ptr<ipt_per_trace_state_t[]> per_trace_state_;

  zx::bti bti_;
};

static uint32_t ipt_config_family;
static uint32_t ipt_config_model;
static uint32_t ipt_config_stepping;

static uint32_t ipt_config_addr_cfg_max = 0;
static uint32_t ipt_config_mtc_freq_mask = 0;
static uint32_t ipt_config_cyc_thresh_mask = 0;
static uint32_t ipt_config_psb_freq_mask = 0;
static uint32_t ipt_config_num_addr_ranges = 0;
static uint32_t ipt_config_bus_freq = 0;

static bool ipt_config_supported = false;

static bool ipt_config_cr3_filtering = false;
static bool ipt_config_psb = false;
static bool ipt_config_ip_filtering = false;
static bool ipt_config_mtc = false;
static bool ipt_config_ptwrite = false;
static bool ipt_config_power_events = false;
static bool ipt_config_output_topa = false;
static bool ipt_config_output_topa_multi = false;
static bool ipt_config_output_single = false;
static bool ipt_config_output_transport = false;
static bool ipt_config_lip = false;

// maximum space, in bytes, for trace buffers (per cpu)
// This isn't necessarily
// MAX_NUM_CHUNKS * (1 << (MAX_CHUNK_ORDER + PAGE_SIZE_SHIFT)).
// Buffers have to be naturally aligned contiguous pages, but we can have
// a lot of them. Supporting large buffers and/or lots of them is for
// experimentation.
#define MAX_PER_TRACE_SPACE (256 * 1024 * 1024)

// maximum number of buffers
#define MAX_NUM_CHUNKS 4096

// maximum size of each buffer, in pages (1MB)
#define MAX_CHUNK_ORDER 8

#if PAGE_SIZE == 4096
#define PAGE_SIZE_SHIFT 12
#else
#error "unsupported page size"
#endif

#define BIT(x, b) ((x) & (1u << (b)))

// The userspace side of the driver

static zx_status_t InsntraceInitOnce() {
  unsigned a, b, c, d, max_leaf;

  max_leaf = __get_cpuid_max(0, nullptr);
  if (max_leaf < 0x14) {
    zxlogf(INFO, "IntelPT: No PT support");
    return ZX_ERR_NOT_SUPPORTED;
  }

  __cpuid(1, a, b, c, d);
  ipt_config_stepping = a & 0xf;
  ipt_config_model = (a >> 4) & 0xf;
  ipt_config_family = (a >> 8) & 0xf;
  if (ipt_config_family == 0xf)
    ipt_config_family += (a >> 20) & 0xff;
  if (ipt_config_family == 6 || ipt_config_family == 0xf)
    ipt_config_model += ((a >> 16) & 0xf) << 4;

  __cpuid_count(0x07, 0, a, b, c, d);
  if (!BIT(b, 25)) {
    zxlogf(INFO, "IntelPT: No PT support");
    return ZX_ERR_NOT_SUPPORTED;
  }

  ipt_config_supported = true;

  __cpuid_count(0x14, 0, a, b, c, d);
  if (BIT(b, 2))
    ipt_config_addr_cfg_max = 2;
  if (BIT(b, 1) && a >= 1) {
    unsigned a1, b1, c1, d1;
    __cpuid_count(0x14, 1, a1, b1, c1, d1);
    ipt_config_mtc_freq_mask = (a1 >> 16) & 0xffff;
    ipt_config_cyc_thresh_mask = b1 & 0xffff;
    ipt_config_psb_freq_mask = (b1 >> 16) & 0xffff;
    ipt_config_num_addr_ranges = a1 & 0x7;
  }

  if (max_leaf >= 0x15) {
    unsigned a1 = 0, b1 = 0, c1 = 0, d1 = 0;
    __cpuid(0x15, a1, b1, c1, d1);
    if (a1 && b1)
      ipt_config_bus_freq = static_cast<uint32_t>(1. / ((float)a1 / (float)b1));
  }

  ipt_config_cr3_filtering = !!BIT(b, 0);
  ipt_config_psb = !!BIT(b, 1);
  ipt_config_ip_filtering = !!BIT(b, 2);
  ipt_config_mtc = !!BIT(b, 3);
  ipt_config_ptwrite = !!BIT(b, 4);
  ipt_config_power_events = !!BIT(b, 5);

  ipt_config_output_topa = !!BIT(c, 0);
  ipt_config_output_topa_multi = !!BIT(c, 1);
  ipt_config_output_single = !!BIT(c, 2);
  ipt_config_output_transport = !!BIT(c, 3);
  ipt_config_lip = !!BIT(c, 31);

  zxlogf(INFO, "Intel Processor Trace configuration for this chipset:");
  // No need to print everything, but these are useful.
  zxlogf(INFO, "mtc_freq_mask:   0x%x", ipt_config_mtc_freq_mask);
  zxlogf(INFO, "cyc_thresh_mask: 0x%x", ipt_config_cyc_thresh_mask);
  zxlogf(INFO, "psb_freq_mask:   0x%x", ipt_config_psb_freq_mask);
  zxlogf(INFO, "num addr ranges: %u", ipt_config_num_addr_ranges);

  return ZX_OK;
}

// Create the ToPA for the configured number of pages for |cpu|.
// A circular collection of buffers is set up, even if we're going to apply
// the stop bit to the last entry.
void InsntraceDevice::MakeTopa(ipt_per_trace_state_t* per_trace) {
  const size_t run_len_log2 = per_trace->chunk_order;
  assert(run_len_log2 + PAGE_SIZE_SHIFT <= IPT_TOPA_MAX_SHIFT);
  assert(run_len_log2 + PAGE_SIZE_SHIFT >= IPT_TOPA_MIN_SHIFT);

  uint32_t curr_table = 0;
  uint32_t curr_idx = 0;
  uint64_t* last_entry = nullptr;

  // Note: An early version of this patch auto-computed the desired grouping
  // of pages with sufficient alignment. If you find yourself needing this
  // functionality again, see change 9470.

  for (uint32_t i = 0; i < per_trace->num_chunks; ++i) {
    io_buffer_t* buffer = &per_trace->chunks[i];
    io_buffer_t* topa = &per_trace->topas[curr_table];
    zx_paddr_t pa = io_buffer_phys(buffer);

    uint64_t val =
        IPT_TOPA_ENTRY_PHYS_ADDR(pa) | IPT_TOPA_ENTRY_SIZE(run_len_log2 + PAGE_SIZE_SHIFT);
    auto table = reinterpret_cast<uint64_t*>(io_buffer_virt(topa));
    table[curr_idx] = val;
    last_entry = &table[curr_idx];

    // Make sure we leave one at the end of the table for the END marker.
    if (unlikely(curr_idx >= IPT_TOPA_MAX_TABLE_ENTRIES - 2)) {
      curr_idx = 0;
      curr_table++;
    } else {
      curr_idx++;
    }
  }

  assert(curr_table + 1 == per_trace->num_tables ||
         // If the last table is full curr_table will be the next one.
         (curr_table == per_trace->num_tables && curr_idx == 0));

  // Populate END entries for completed tables
  // Assume the table is circular. We'll set the stop bit on the last
  // entry later.
  for (uint32_t i = 0; i < curr_table; ++i) {
    io_buffer_t* this_table = &per_trace->topas[i];
    io_buffer_t* next_table;
    if (i == per_trace->num_tables - 1) {
      next_table = &per_trace->topas[0];
    } else {
      next_table = &per_trace->topas[i + 1];
    }

    zx_paddr_t next_table_pa = io_buffer_phys(next_table);
    uint64_t val = IPT_TOPA_ENTRY_PHYS_ADDR(next_table_pa) | IPT_TOPA_ENTRY_END;
    auto table = reinterpret_cast<uint64_t*>(io_buffer_virt(this_table));
    table[IPT_TOPA_MAX_TABLE_ENTRIES - 1] = val;
  }

  // Populate the END entry for a possibly non-full last table
  if (curr_table < per_trace->num_tables) {
    io_buffer_t* this_table = &per_trace->topas[curr_table];
    io_buffer_t* first_table = &per_trace->topas[0];
    zx_paddr_t first_table_pa = io_buffer_phys(first_table);
    uint64_t val = IPT_TOPA_ENTRY_PHYS_ADDR(first_table_pa) | IPT_TOPA_ENTRY_END;
    auto table = reinterpret_cast<uint64_t*>(io_buffer_virt(this_table));
    table[curr_idx] = val;
  }

  // Add the STOP flag to the last non-END entry in the tables
  assert(last_entry);
  if (!per_trace->is_circular)
    *last_entry |= IPT_TOPA_ENTRY_STOP;
}

// Compute the number of ToPA entries needed for the configured number of
// buffers.
// The output count includes the END entries across all needed tables.
uint32_t InsntraceDevice::ComputeTopaEntryCount(ipt_per_trace_state_t* per_trace) {
  uint32_t num_entries = per_trace->num_chunks;
  uint32_t num_end_entries =
      (num_entries + IPT_TOPA_MAX_TABLE_ENTRIES - 2) / (IPT_TOPA_MAX_TABLE_ENTRIES - 1);
  uint32_t result = num_entries + num_end_entries;

  zxlogf(TRACE, "IPT: compute_topa_entry_count: num_entries: %u", num_entries);
  zxlogf(TRACE, "IPT: compute_topa_entry_count: num_end_entries: %u", num_end_entries);
  zxlogf(TRACE, "IPT: compute_topa_entry_count: total entries: %u", result);

  return result;
}

// Walk the tables to discover how much data has been captured for |per_trace|.
// Note: If this is a circular buffer this is just where tracing stopped.
size_t InsntraceDevice::ComputeCaptureSize(const ipt_per_trace_state_t* per_trace) {
  uint64_t curr_table_paddr = per_trace->output_base;
  uint32_t curr_table_entry_idx = (uint32_t)per_trace->output_mask_ptrs >> 7;
  uint32_t curr_entry_offset = (uint32_t)(per_trace->output_mask_ptrs >> 32);

  zxlogf(TRACE, "IPT: compute_capture_size: trace %tu", per_trace - per_trace_state_.get());
  zxlogf(TRACE,
         "IPT: curr_table_paddr 0x%" PRIx64 ", curr_table_entry_idx %u, curr_entry_offset %u\n",
         curr_table_paddr, curr_table_entry_idx, curr_entry_offset);

  size_t total_size = 0;
  for (uint32_t table = 0; table < per_trace->num_tables; ++table) {
    // Get the physical address so that we can compare it with the value
    // in output_base.
    zx_paddr_t table_paddr = io_buffer_phys(&per_trace->topas[table]);

    for (uint32_t entry = 0; entry < IPT_TOPA_MAX_TABLE_ENTRIES - 1; ++entry) {
      if (table_paddr == curr_table_paddr && entry >= curr_table_entry_idx) {
        total_size += curr_entry_offset;
        return total_size;
      }
      auto table_ptr = reinterpret_cast<uint64_t*>(io_buffer_virt(&per_trace->topas[table]));
      uint64_t topa_entry = table_ptr[entry];
      total_size += 1UL << IPT_TOPA_ENTRY_EXTRACT_SIZE(topa_entry);
    }
  }

  // Should be unreachable.
  // TODO(dje): Later flag state as broken.
  zxlogf(ERROR, "IPT: unexpectedly exited capture loop");
  return 0;
}

zx_status_t InsntraceDevice::X86PtAllocBuffer1(ipt_per_trace_state_t* per_trace, uint32_t num,
                                               uint32_t order, bool is_circular) {
  zx_status_t status;
  size_t chunk_pages = 1 << order;

  fbl::AllocChecker ac;
  per_trace->chunks = std::unique_ptr<io_buffer_t[]>(new (&ac) io_buffer_t[num]{});
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  for (uint32_t i = 0; i < num; ++i) {
    // ToPA entries of size N must be aligned to N, too.
    uint32_t alignment_log2 = PAGE_SIZE_SHIFT + order;
    status = io_buffer_init_aligned(&per_trace->chunks[i], bti_.get(), chunk_pages * PAGE_SIZE,
                                    alignment_log2, IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status != ZX_OK)
      return status;
    // Keep track of allocated buffers as we go in case we later fail:
    // we want to be able to free those that got allocated.
    ++per_trace->num_chunks;
    // Catch bugs in io_buffer_init_aligned. If it doesn't give us a
    // properly aligned buffer we'll get an "operational error" later.
    // See Intel Vol3 36.2.6.2.
    zx_paddr_t pa = io_buffer_phys(&per_trace->chunks[i]);
    zx_paddr_t align_mask = (1ull << alignment_log2) - 1;
    if (pa & align_mask) {
      zxlogf(ERROR, "%s: WARNING: chunk has bad alignment: alignment %u, got 0x%" PRIx64 "",
             __func__, alignment_log2, pa);
      return ZX_ERR_INTERNAL;
    }
  }
  assert(per_trace->num_chunks == num);

  per_trace->chunk_order = order;
  per_trace->is_circular = is_circular;

  // TODO(dje): No need to allocate the max on the last table.
  uint32_t entry_count = ComputeTopaEntryCount(per_trace);
  uint32_t table_count =
      (entry_count + IPT_TOPA_MAX_TABLE_ENTRIES - 1) / IPT_TOPA_MAX_TABLE_ENTRIES;

  if (entry_count < 2) {
    zxlogf(INFO, "IPT: INVALID ENTRY COUNT: %u", entry_count);
    return ZX_ERR_INVALID_ARGS;
  }

  // Some early Processor Trace implementations only supported having a
  // table with a single real entry and an END.
  if (!ipt_config_output_topa_multi && entry_count > 2)
    return ZX_ERR_NOT_SUPPORTED;

  // Allocate Table(s) of Physical Addresses (ToPA) for each cpu.

  per_trace->topas = std::unique_ptr<io_buffer_t[]>(new (&ac) io_buffer_t[table_count]{});
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  for (uint32_t i = 0; i < table_count; ++i) {
    status = io_buffer_init(&per_trace->topas[i], bti_.get(),
                            sizeof(uint64_t) * IPT_TOPA_MAX_TABLE_ENTRIES,
                            IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status != ZX_OK)
      return ZX_ERR_NO_MEMORY;
    // Keep track of allocated tables as we go in case we later fail:
    // we want to be able to free those that got allocated.
    ++per_trace->num_tables;
  }
  assert(per_trace->num_tables == table_count);

  MakeTopa(per_trace);

  return ZX_OK;
}

void InsntraceDevice::X86PtFreeBuffer1(ipt_per_trace_state_t* per_trace) {
  assert(!per_trace->assigned);

  if (per_trace->chunks) {
    for (uint32_t i = 0; i < per_trace->num_chunks; ++i) {
      io_buffer_release(&per_trace->chunks[i]);
    }
  }
  per_trace->chunks.reset();

  if (per_trace->topas) {
    for (uint32_t i = 0; i < per_trace->num_tables; ++i) {
      io_buffer_release(&per_trace->topas[i]);
    }
  }
  per_trace->topas.reset();

  per_trace->allocated = false;
}

zx_status_t InsntraceDevice::X86PtAllocBuffer(const BufferConfig* config,
                                              BufferDescriptor* out_descriptor) {
  zxlogf(TRACE, "%s: num_chunks %u, chunk_order %u", __func__, config->num_chunks,
         config->chunk_order);

  if (config->num_chunks == 0 || config->num_chunks > MAX_NUM_CHUNKS)
    return ZX_ERR_INVALID_ARGS;
  if (config->chunk_order > MAX_CHUNK_ORDER)
    return ZX_ERR_INVALID_ARGS;
  size_t chunk_pages = 1 << config->chunk_order;
  size_t nr_pages = config->num_chunks * chunk_pages;
  size_t total_per_trace = nr_pages * PAGE_SIZE;
  if (total_per_trace > MAX_PER_TRACE_SPACE)
    return ZX_ERR_INVALID_ARGS;

  uint64_t settable_ctl_mask =
      (IPT_CTL_OS_ALLOWED_MASK | IPT_CTL_USER_ALLOWED_MASK | IPT_CTL_TSC_EN_MASK |
       IPT_CTL_DIS_RETC_MASK | IPT_CTL_BRANCH_EN_MASK);
  if (ipt_config_ptwrite)
    settable_ctl_mask |= IPT_CTL_PTW_EN_MASK | IPT_CTL_FUP_ON_PTW_MASK;
  if (ipt_config_cr3_filtering)
    settable_ctl_mask |= IPT_CTL_CR3_FILTER_MASK;
  if (ipt_config_mtc)
    settable_ctl_mask |= IPT_CTL_MTC_EN_MASK | IPT_CTL_MTC_FREQ_MASK;
  if (ipt_config_power_events)
    settable_ctl_mask |= IPT_CTL_POWER_EVENT_EN_MASK;
  if (ipt_config_ip_filtering) {
    if (ipt_config_num_addr_ranges >= 1)
      settable_ctl_mask |= IPT_CTL_ADDR0_MASK;
    if (ipt_config_num_addr_ranges >= 2)
      settable_ctl_mask |= IPT_CTL_ADDR1_MASK;
    if (ipt_config_num_addr_ranges >= 3)
      settable_ctl_mask |= IPT_CTL_ADDR2_MASK;
    if (ipt_config_num_addr_ranges >= 4)
      settable_ctl_mask |= IPT_CTL_ADDR3_MASK;
  }
  if (ipt_config_psb)
    settable_ctl_mask |= (IPT_CTL_CYC_EN_MASK | IPT_CTL_PSB_FREQ_MASK | IPT_CTL_CYC_THRESH_MASK);
  if ((config->ctl & ~settable_ctl_mask) != 0) {
    zxlogf(ERROR, "bad ctl, requested 0x%" PRIx64 ", valid 0x%" PRIx64 "", config->ctl,
           settable_ctl_mask);
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t mtc_freq = (uint32_t)((config->ctl & IPT_CTL_MTC_FREQ_MASK) >> IPT_CTL_MTC_FREQ_SHIFT);
  if (mtc_freq != 0 && ((1 << mtc_freq) & ipt_config_mtc_freq_mask) == 0) {
    zxlogf(ERROR, "bad mtc_freq value, requested 0x%x, valid mask 0x%x", mtc_freq,
           ipt_config_mtc_freq_mask);
    return ZX_ERR_INVALID_ARGS;
  }
  uint32_t cyc_thresh =
      (uint32_t)((config->ctl & IPT_CTL_CYC_THRESH_MASK) >> IPT_CTL_CYC_THRESH_SHIFT);
  if (cyc_thresh != 0 && ((1 << cyc_thresh) & ipt_config_cyc_thresh_mask) == 0) {
    zxlogf(ERROR, "bad cyc_thresh value, requested 0x%x, valid mask 0x%x", cyc_thresh,
           ipt_config_cyc_thresh_mask);
    return ZX_ERR_INVALID_ARGS;
  }
  uint32_t psb_freq = (uint32_t)((config->ctl & IPT_CTL_PSB_FREQ_MASK) >> IPT_CTL_PSB_FREQ_SHIFT);
  if (psb_freq != 0 && ((1 << psb_freq) & ipt_config_psb_freq_mask) == 0) {
    zxlogf(ERROR, "bad psb_freq value, requested 0x%x, valid mask 0x%x", psb_freq,
           ipt_config_psb_freq_mask);
    return ZX_ERR_INVALID_ARGS;
  }

  // Find an unallocated buffer entry.
  BufferDescriptor descriptor;
  for (descriptor = 0; descriptor < num_traces_; ++descriptor) {
    if (!per_trace_state_[descriptor].allocated)
      break;
  }
  if (descriptor == num_traces_)
    return ZX_ERR_NO_RESOURCES;

  ipt_per_trace_state_t* per_trace = &per_trace_state_[descriptor];
  zx_status_t status =
      X86PtAllocBuffer1(per_trace, config->num_chunks, config->chunk_order, config->is_circular);
  if (status != ZX_OK) {
    X86PtFreeBuffer1(per_trace);
    return status;
  }

  per_trace->ctl = config->ctl;
  per_trace->status = 0;
  per_trace->output_base = io_buffer_phys(&per_trace->topas[0]);
  per_trace->output_mask_ptrs = 0;
  per_trace->cr3_match = config->address_space_match;

  // TODO(dje): insntrace.fidl can't use vectors (yet) so the address ranges
  // are individually spelled out.
  static_assert(fuchsia_insntrace::wire::MAX_NUM_ADDR_RANGES == 2);
  static_assert(fuchsia_insntrace::wire::MAX_NUM_ADDR_RANGES == IPT_MAX_NUM_ADDR_RANGES);
  per_trace->addr_ranges[0].a = config->address_range_0.start;
  per_trace->addr_ranges[0].b = config->address_range_0.end;
  per_trace->addr_ranges[1].a = config->address_range_1.start;
  per_trace->addr_ranges[1].b = config->address_range_1.end;

  per_trace->allocated = true;
  *out_descriptor = descriptor;
  return ZX_OK;
}

zx_status_t InsntraceDevice::X86PtAssignThreadBuffer(BufferDescriptor descriptor,
                                                     zx_handle_t thread) {
  zx_handle_close(thread);
  // TODO(dje): Thread support is still work-in-progress.
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t InsntraceDevice::X86PtReleaseThreadBuffer(BufferDescriptor descriptor,
                                                      zx_handle_t thread) {
  zx_handle_close(thread);
  // TODO(dje): Thread support is still work-in-progress.
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t InsntraceDevice::X86PtFreeBuffer(BufferDescriptor descriptor) {
  if (active_)
    return ZX_ERR_BAD_STATE;
  if (descriptor >= num_traces_)
    return ZX_ERR_INVALID_ARGS;
  assert(per_trace_state_);
  ipt_per_trace_state_t* per_trace = &per_trace_state_[descriptor];

  if (!per_trace->allocated)
    return ZX_ERR_INVALID_ARGS;
  if (per_trace->assigned)
    return ZX_ERR_BAD_STATE;
  X86PtFreeBuffer1(per_trace);
  return ZX_OK;
}

zx_status_t InsntraceDevice::X86PtStageTraceData(zx_handle_t resource,
                                                 BufferDescriptor descriptor) {
  if (descriptor >= num_traces_)
    return ZX_ERR_INVALID_ARGS;
  assert(per_trace_state_);
  const ipt_per_trace_state_t* per_trace = &per_trace_state_[descriptor];

  zx_x86_pt_regs_t regs;
  regs.ctl = per_trace->ctl;
  regs.ctl |= IPT_CTL_TOPA_MASK | IPT_CTL_TRACE_EN_MASK;
  regs.status = per_trace->status;
  regs.output_base = per_trace->output_base;
  regs.output_mask_ptrs = per_trace->output_mask_ptrs;
  regs.cr3_match = per_trace->cr3_match;
  static_assert(sizeof(regs.addr_ranges) == sizeof(per_trace->addr_ranges),
                "addr range size mismatch");
  memcpy(regs.addr_ranges, per_trace->addr_ranges, sizeof(per_trace->addr_ranges));

  return zx_mtrace_control(resource, MTRACE_KIND_INSNTRACE, MTRACE_INSNTRACE_STAGE_TRACE_DATA,
                           descriptor, &regs, sizeof(regs));
}

zx_status_t InsntraceDevice::X86PtGetTraceData(zx_handle_t resource, BufferDescriptor descriptor) {
  if (descriptor >= num_traces_)
    return ZX_ERR_INVALID_ARGS;
  assert(per_trace_state_);
  ipt_per_trace_state_t* per_trace = &per_trace_state_[descriptor];

  zx_x86_pt_regs_t regs;
  zx_status_t status =
      zx_mtrace_control(resource, MTRACE_KIND_INSNTRACE, MTRACE_INSNTRACE_GET_TRACE_DATA,
                        descriptor, &regs, sizeof(regs));
  if (status != ZX_OK)
    return status;
  per_trace->ctl = regs.ctl;
  per_trace->status = regs.status;
  per_trace->output_base = regs.output_base;
  per_trace->output_mask_ptrs = regs.output_mask_ptrs;
  per_trace->cr3_match = regs.cr3_match;
  static_assert(sizeof(per_trace->addr_ranges) == sizeof(regs.addr_ranges),
                "addr range size mismatch");
  memcpy(per_trace->addr_ranges, regs.addr_ranges, sizeof(regs.addr_ranges));

  return ZX_OK;
}

// fidl message handlers

zx_status_t InsntraceDevice::IptInitialize(const fuchsia_insntrace::wire::Allocation* allocation) {
  if (!ipt_config_supported)
    return ZX_ERR_NOT_SUPPORTED;
  // For now we only support ToPA, though there are no current plans to
  // support anything else.
  if (!ipt_config_output_topa)
    return ZX_ERR_NOT_SUPPORTED;
  if (per_trace_state_)
    return ZX_ERR_BAD_STATE;

  // TODO(dje): Until thread tracing is supported.
  if (allocation->mode == fuchsia_insntrace::wire::Mode::THREAD)
    return ZX_ERR_NOT_SUPPORTED;

  zx_insntrace_trace_mode_t internal_mode;
  switch (allocation->mode) {
    case fuchsia_insntrace::wire::Mode::CPU:
      internal_mode = IPT_MODE_CPU;
      break;
    case fuchsia_insntrace::wire::Mode::THREAD:
      internal_mode = IPT_MODE_THREAD;
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }

  if (allocation->num_traces > fuchsia_insntrace::wire::MAX_NUM_TRACES)
    return ZX_ERR_INVALID_ARGS;
  if (internal_mode == IPT_MODE_CPU) {
    // TODO(dje): KISS. No point in allowing anything else for now.
    if (allocation->num_traces != zx_system_get_num_cpus())
      return ZX_ERR_INVALID_ARGS;
  }

  fbl::AllocChecker ac;
  auto per_trace_state = new (&ac) ipt_per_trace_state_t[allocation->num_traces]{};
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  per_trace_state_.reset(per_trace_state);

  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx_handle_t resource = get_root_resource();
  zx_insntrace_trace_config_t config{};
  config.mode = internal_mode;
  config.num_traces = allocation->num_traces;
  zx_status_t status = zx_mtrace_control(resource, MTRACE_KIND_INSNTRACE,
                                         MTRACE_INSNTRACE_ALLOC_TRACE, 0, &config, sizeof(config));
  if (status != ZX_OK) {
    per_trace_state_.reset();
    return status;
  }

  mode_ = internal_mode;
  num_traces_ = allocation->num_traces;
  return ZX_OK;
}

zx_status_t InsntraceDevice::IptTerminate() {
  if (!per_trace_state_)
    return ZX_ERR_BAD_STATE;
  if (active_)
    return ZX_ERR_BAD_STATE;

  // Don't make any changes until we know it's going to work.
  for (uint32_t i = 0; i < num_traces_; ++i) {
    ipt_per_trace_state_t* per_trace = &per_trace_state_[i];
    if (per_trace->assigned)
      return ZX_ERR_BAD_STATE;
  }

  for (uint32_t i = 0; i < num_traces_; ++i) {
    ipt_per_trace_state_t* per_trace = &per_trace_state_[i];
    if (per_trace->allocated)
      X86PtFreeBuffer1(per_trace);
  }

  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx_handle_t resource = get_root_resource();
  zx_status_t status = zx_mtrace_control(resource, MTRACE_KIND_INSNTRACE,
                                         MTRACE_INSNTRACE_FREE_TRACE, 0, nullptr, 0);
  // TODO(dje): This really shouldn't fail. What to do?
  // For now flag things as busted and prevent further use.
  if (status != ZX_OK)
    return ZX_OK;

  per_trace_state_.reset();
  return ZX_OK;
}

zx_status_t InsntraceDevice::IptGetAllocation(fuchsia_insntrace::wire::Allocation* out_config) {
  if (!per_trace_state_)
    return ZX_ERR_BAD_STATE;
  switch (mode_) {
    case IPT_MODE_CPU:
      out_config->mode = fuchsia_insntrace::wire::Mode::CPU;
      break;
    case IPT_MODE_THREAD:
      out_config->mode = fuchsia_insntrace::wire::Mode::THREAD;
      break;
    default:
      __UNREACHABLE;
  }
  out_config->num_traces = num_traces_;
  return ZX_OK;
}

zx_status_t InsntraceDevice::IptAllocateBuffer(const BufferConfig* config,
                                               BufferDescriptor* out_descriptor) {
  if (!per_trace_state_)
    return ZX_ERR_BAD_STATE;
  return X86PtAllocBuffer(config, out_descriptor);
}

zx_status_t InsntraceDevice::IptAssignThreadBuffer(BufferDescriptor descriptor,
                                                   zx_handle_t thread) {
  if (!per_trace_state_)
    return ZX_ERR_BAD_STATE;
  return X86PtAssignThreadBuffer(descriptor, thread);
}

zx_status_t InsntraceDevice::IptReleaseThreadBuffer(BufferDescriptor descriptor,
                                                    zx_handle_t thread) {
  if (!per_trace_state_)
    return ZX_ERR_BAD_STATE;
  return X86PtReleaseThreadBuffer(descriptor, thread);
}

zx_status_t InsntraceDevice::IptGetBufferConfig(BufferDescriptor descriptor,
                                                BufferConfig* out_config) {
  if (!per_trace_state_)
    return ZX_ERR_BAD_STATE;
  if (descriptor >= num_traces_)
    return ZX_ERR_INVALID_ARGS;
  const ipt_per_trace_state_t* per_trace = &per_trace_state_[descriptor];
  if (!per_trace->allocated)
    return ZX_ERR_INVALID_ARGS;

  *out_config = {};
  out_config->num_chunks = per_trace->num_chunks;
  out_config->chunk_order = per_trace->chunk_order;
  out_config->is_circular = per_trace->is_circular;
  out_config->ctl = per_trace->ctl;
  out_config->address_space_match = per_trace->cr3_match;
  // TODO(dje): insntrace.fidl can't use vectors (yet) so the address ranges
  // are individually spelled out.
  static_assert(fuchsia_insntrace::wire::MAX_NUM_ADDR_RANGES == 2);
  static_assert(fuchsia_insntrace::wire::MAX_NUM_ADDR_RANGES == IPT_MAX_NUM_ADDR_RANGES);
  out_config->address_range_0.start = per_trace->addr_ranges[0].a;
  out_config->address_range_0.end = per_trace->addr_ranges[0].b;
  out_config->address_range_1.start = per_trace->addr_ranges[1].a;
  out_config->address_range_1.end = per_trace->addr_ranges[1].b;

  return ZX_OK;
}

zx_status_t InsntraceDevice::IptGetBufferState(BufferDescriptor descriptor,
                                               BufferState* out_state) {
  if (!per_trace_state_)
    return ZX_ERR_BAD_STATE;

  // In thread-mode we need to get buffer info while tracing is active.
  if (mode_ == IPT_MODE_CPU && active_)
    return ZX_ERR_BAD_STATE;

  if (descriptor >= num_traces_)
    return ZX_ERR_INVALID_ARGS;
  const ipt_per_trace_state_t* per_trace = &per_trace_state_[descriptor];
  if (!per_trace->allocated)
    return ZX_ERR_INVALID_ARGS;

  // Note: If this is a circular buffer this is just where tracing stopped.
  *out_state = {};
  out_state->capture_end = ComputeCaptureSize(per_trace);
  return ZX_OK;
}

zx_status_t InsntraceDevice::IptGetChunkHandle(BufferDescriptor descriptor, uint32_t chunk_num,
                                               zx_handle_t* out_handle) {
  if (!per_trace_state_)
    return ZX_ERR_BAD_STATE;

  if (descriptor >= num_traces_)
    return ZX_ERR_INVALID_ARGS;
  const ipt_per_trace_state_t* per_trace = &per_trace_state_[descriptor];
  if (!per_trace->allocated)
    return ZX_ERR_INVALID_ARGS;
  if (chunk_num >= per_trace->num_chunks)
    return ZX_ERR_INVALID_ARGS;

  zx_handle_t vmo_handle = per_trace->chunks[chunk_num].vmo_handle;
  zx_info_handle_basic_t handle_info;
  zx_status_t status = zx_object_get_info(vmo_handle, ZX_INFO_HANDLE_BASIC, &handle_info,
                                          sizeof(handle_info), nullptr, nullptr);
  if (status != ZX_OK) {
    // This could only fail if vmo_handle is invalid.
    printf("%s: WARNING: unexpected error reading vmo handle rights: %d/%s\n", __func__, status,
           zx_status_get_string(status));
    return status;
  }
  zx_rights_t allowed_rights = (ZX_RIGHT_TRANSFER | ZX_RIGHT_WAIT | ZX_RIGHT_INSPECT |
                                ZX_RIGHT_GET_PROPERTY | ZX_RIGHT_READ | ZX_RIGHT_MAP);
  return zx_handle_duplicate(vmo_handle, handle_info.rights & allowed_rights, out_handle);
}

zx_status_t InsntraceDevice::IptFreeBuffer(BufferDescriptor descriptor) {
  if (!per_trace_state_)
    return ZX_ERR_BAD_STATE;

  return X86PtFreeBuffer(descriptor);
}

// Begin tracing.
// This is basically a nop in thread mode, it is still used for thread-mode
// for consistency and in case we some day need it to do something.

zx_status_t InsntraceDevice::IptStart() {
  if (!per_trace_state_)
    return ZX_ERR_BAD_STATE;
  if (active_)
    return ZX_ERR_BAD_STATE;
  if (mode_ != IPT_MODE_CPU)
    return ZX_ERR_BAD_STATE;

  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx_handle_t resource = get_root_resource();
  zx_status_t status;

  // In cpu-mode, until we support tracing particular cpus, auto-assign
  // buffers to each cpu.
  if (mode_ == IPT_MODE_CPU) {
    // First verify a buffer has been allocated for each cpu,
    // and not yet assigned.
    for (uint32_t cpu = 0; cpu < num_traces_; ++cpu) {
      const ipt_per_trace_state_t* per_trace = &per_trace_state_[cpu];
      if (!per_trace->allocated)
        return ZX_ERR_BAD_STATE;
      if (per_trace->assigned)
        return ZX_ERR_BAD_STATE;
    }

    for (uint32_t cpu = 0; cpu < num_traces_; ++cpu) {
      status = X86PtStageTraceData(resource, cpu);
      if (status != ZX_OK) {
        // TODO(dje): Unstage ones already done.
        return status;
      }
      ipt_per_trace_state_t* per_trace = &per_trace_state_[cpu];
      per_trace->owner.cpu = cpu;
      per_trace->assigned = true;
    }
  }

  status =
      zx_mtrace_control(resource, MTRACE_KIND_INSNTRACE, MTRACE_INSNTRACE_START, 0, nullptr, 0);
  if (status != ZX_OK)
    return status;
  active_ = true;
  return ZX_OK;
}

// Stop tracing.
// In thread-mode all buffers must be released first. That is how we know that
// if we return ZX_OK then all threads are no longer being traced. Otherwise,
// this is basically a nop in thread-mode.

zx_status_t InsntraceDevice::IptStop() {
  if (!per_trace_state_)
    return ZX_ERR_BAD_STATE;
  if (!active_)
    return ZX_ERR_BAD_STATE;

  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx_handle_t resource = get_root_resource();

  zx_status_t status =
      zx_mtrace_control(resource, MTRACE_KIND_INSNTRACE, MTRACE_INSNTRACE_STOP, 0, nullptr, 0);
  if (status != ZX_OK)
    return status;
  active_ = false;

  // Until we support tracing individual cpus, auto-unassign the buffers
  // in cpu-mode.
  if (mode_ == IPT_MODE_CPU) {
    for (uint32_t cpu = 0; cpu < num_traces_; ++cpu) {
      status = X86PtGetTraceData(resource, cpu);
      if (status != ZX_OK)
        return status;
      ipt_per_trace_state_t* per_trace = &per_trace_state_[cpu];
      per_trace->assigned = false;
      per_trace->owner.cpu = 0;
      // If there was an operational error, report it.
      if (per_trace->status & IPT_STATUS_ERROR_MASK) {
        printf("%s: WARNING: operational error detected on cpu %d\n", __func__, cpu);
      }
    }
  }

  return ZX_OK;
}

// Fidl interface.

void InsntraceDevice::Initialize(fuchsia_insntrace::wire::Allocation allocation,
                                 InitializeCompleter::Sync& completer) {
  zx_status_t status = IptInitialize(&allocation);
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void InsntraceDevice::Terminate(TerminateCompleter::Sync& completer) {
  zx_status_t status = IptTerminate();
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void InsntraceDevice::GetAllocation(GetAllocationCompleter::Sync& completer) {
  fuchsia_insntrace::wire::Allocation config{};
  zx_status_t status = IptGetAllocation(&config);
  completer.Reply(status == ZX_OK ? fidl::unowned_ptr(&config) : nullptr);
}

void InsntraceDevice::AllocateBuffer(BufferConfig config,
                                     AllocateBufferCompleter::Sync& completer) {
  BufferDescriptor descriptor;
  zx_status_t status = IptAllocateBuffer(&config, &descriptor);
  if (status == ZX_OK) {
    completer.ReplySuccess(descriptor);
  } else {
    completer.ReplyError(status);
  }
}

void InsntraceDevice::AssignThreadBuffer(BufferDescriptor descriptor, zx::thread thread,
                                         AssignThreadBufferCompleter::Sync& completer) {
  zx_status_t status = IptAssignThreadBuffer(thread.release(), descriptor);
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void InsntraceDevice::ReleaseThreadBuffer(BufferDescriptor descriptor, zx::thread thread,
                                          ReleaseThreadBufferCompleter::Sync& completer) {
  zx_status_t status = IptReleaseThreadBuffer(thread.release(), descriptor);
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void InsntraceDevice::GetBufferConfig(BufferDescriptor descriptor,
                                      GetBufferConfigCompleter::Sync& completer) {
  BufferConfig config;
  zx_status_t status = IptGetBufferConfig(descriptor, &config);
  completer.Reply(status == ZX_OK ? fidl::unowned_ptr(&config) : nullptr);
}

void InsntraceDevice::GetBufferState(BufferDescriptor descriptor,
                                     GetBufferStateCompleter::Sync& completer) {
  BufferState state;
  zx_status_t status = IptGetBufferState(descriptor, &state);
  completer.Reply(status == ZX_OK ? fidl::unowned_ptr(&state) : nullptr);
}

void InsntraceDevice::GetChunkHandle(BufferDescriptor descriptor, uint32_t chunk_num,
                                     GetChunkHandleCompleter::Sync& completer) {
  zx_handle_t handle;
  zx_status_t status = IptGetChunkHandle(descriptor, chunk_num, &handle);
  completer.Reply(zx::vmo(status == ZX_OK ? handle : ZX_HANDLE_INVALID));
}

void InsntraceDevice::FreeBuffer(BufferDescriptor descriptor,
                                 FreeBufferCompleter::Sync& completer) {
  zx_status_t status = IptFreeBuffer(descriptor);
  if (status == ZX_OK) {
    completer.Reply();
  }
}

void InsntraceDevice::Start(StartCompleter::Sync& completer) {
  zx_status_t status = IptStart();
  if (status == ZX_OK) {
    completer.Reply();
  }
}

void InsntraceDevice::Stop(StopCompleter::Sync& completer) {
  zx_status_t status = IptStop();
  if (status == ZX_OK) {
    completer.Reply();
  }
}

// Devhost interface.

zx_status_t InsntraceDevice::DdkOpen(zx_device_t** dev_out, uint32_t flags) {
  if (opened_)
    return ZX_ERR_ALREADY_BOUND;

  opened_ = true;
  return ZX_OK;
}

zx_status_t InsntraceDevice::DdkClose(uint32_t flags) {
  opened_ = false;
  return ZX_OK;
}

zx_status_t InsntraceDevice::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  mtx_lock(&lock_);
  fuchsia_insntrace::Controller::Dispatch(this, msg, &transaction);
  mtx_unlock(&lock_);
  return transaction.Status();
}

void InsntraceDevice::DdkRelease() {
  IptStop();
  IptTerminate();

  delete this;
}

}  // namespace insntrace

zx_status_t insntrace_bind(void* ctx, zx_device_t* parent) {
  zx_status_t status = insntrace::InsntraceInitOnce();
  if (status != ZX_OK) {
    return status;
  }

  pdev_protocol_t pdev;
  status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev);
  if (status != ZX_OK) {
    return status;
  }

  zx::bti bti;
  status = pdev_get_bti(&pdev, 0, bti.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  fbl::AllocChecker ac;
  auto dev = std::unique_ptr<insntrace::InsntraceDevice>(
      new (&ac) insntrace::InsntraceDevice(parent, std::move(bti)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  status = dev->DdkAdd("insntrace");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: could not add device: %d", __func__, status);
  } else {
    // devmgr owns the memory now
    __UNUSED auto ptr = dev.release();
  }
  return status;
}
