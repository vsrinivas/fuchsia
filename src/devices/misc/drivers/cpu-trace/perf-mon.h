// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_MISC_DRIVERS_CPU_TRACE_PERF_MON_H_
#define SRC_DEVICES_MISC_DRIVERS_CPU_TRACE_PERF_MON_H_

#include <fuchsia/perfmon/cpu/llcpp/fidl.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/io-buffer.h>
#include <lib/zx/bti.h>
#include <stddef.h>
#include <stdint.h>
#include <threads.h>
#include <zircon/types.h>

#include <memory>

#include <ddktl/device.h>

#if defined(__x86_64__)
#include <lib/zircon-internal/device/cpu-trace/intel-pm.h>

#include "intel-pm-impl.h"
#elif defined(__aarch64__)
#include <lib/zircon-internal/device/cpu-trace/arm64-pm.h>

#include "arm64-pm-impl.h"
#else
#error "unsupported architecture"
#endif

#include "cpu-trace-private.h"

namespace perfmon {

namespace fidl_perfmon = fuchsia_perfmon_cpu;

// Shorten some long FIDL names.
using FidlPerfmonAllocation = fidl_perfmon::wire::Allocation;
using FidlPerfmonConfig = fidl_perfmon::wire::Config;
using FidlPerfmonProperties = fidl_perfmon::wire::Properties;
using FidlPerfmonEventConfigFlags = fidl_perfmon::wire::EventConfigFlags;

#if defined(__x86_64__)
using PmuHwProperties = X86PmuProperties;
using PmuConfig = X86PmuConfig;
#elif defined(__aarch64__)
using PmuHwProperties = Arm64PmuProperties;
using PmuConfig = Arm64PmuConfig;
#endif

struct EventDetails {
  // Ids are densely allocated. If ids get larger than this we will need
  // a more complex id->event map.
  uint16_t id;
  uint32_t event;
#ifdef __x86_64__
  uint32_t umask;
#endif
  uint32_t flags;
};

// The minimum value of |PmuCommonProperties.pm_version| we support.
// The chosen value is conservative. Yes, we can support preceding PMU versions with effort,
// but that effort has yet to be warranted.
#if defined(__x86_64__)
// Skylake supports version 4. KISS and begin with that.
// Note: This should agree with the kernel driver's check.
constexpr uint16_t kMinPmVersion = 4;
#elif defined(__aarch64__)
// KISS and begin with pmu v3.
// Note: This should agree with the kernel driver's check.
constexpr uint16_t kMinPmVersion = 3;
#endif

// Compare function to qsort, bsearch.
int ComparePerfmonEventId(const void* ap, const void* bp);

// Return the largest event id in |events,count|.
uint16_t GetLargestEventId(const EventDetails* events, size_t count);

// Build a lookup map for |events,count|.
// The lookup map translates event ids, which is used as the index into the
// map and returns an enum value for the particular event kind.
// Event ids aren't necessarily dense, but the enums are.
zx_status_t BuildEventMap(const EventDetails* events, size_t count, const uint16_t** out_event_map,
                          size_t* out_map_size);

// All configuration data is staged here before writing any MSRs, etc.
// Then when ready the "Start" FIDL call will write all the necessary MSRS,
// and do whatever kernel operations are required for collecting data.

struct PmuPerTraceState {
  // True if |config| has been set.
  bool configured;

  // The trace configuration as given to us via FIDL.
  FidlPerfmonConfig fidl_config;

  // The internalized form of |FidlPerfmonConfig| that we pass to the kernel.
  PmuConfig config;

  // # of entries in |buffers|.
  // TODO(dje): This is generally the number of cpus, but it could be
  // something else later.
  uint32_t num_buffers;

  // The size of each buffer in 4K pages.
  // Each buffer is the same size (at least for now, KISS).
  // There is one buffer per cpu.
  uint32_t buffer_size_in_pages;

  std::unique_ptr<io_buffer_t[]> buffers;
};

// Devhost interface.

// TODO(dje): add unbindable?
class PerfmonDevice;
using DeviceType = ddk::Device<PerfmonDevice, ddk::Openable, ddk::Closable, ddk::MessageableOld>;

class PerfmonDevice : public DeviceType, public fidl::WireServer<fidl_perfmon::Controller> {
 public:
  // The page size we use.
  static constexpr uint32_t kLog2PageSize = 12;
  static constexpr uint32_t kPageSize = 1 << kLog2PageSize;
  // maximum space, in pages, for trace buffers (per cpu)
  static constexpr uint32_t kMaxPerTraceSpaceInPages = (256 * 1024 * 1024) / kPageSize;

  // Fetch the pmu hw properties from the kernel.
  static zx_status_t GetHwProperties(mtrace_control_func_t* mtrace_control,
                                     PmuHwProperties* out_props);

  // Architecture-provided routine to initialize static state.
  // TODO(dje): Move static state to the device object for better testability.
  static zx_status_t InitOnce();

  explicit PerfmonDevice(zx_device_t* parent, zx::bti bti, perfmon::PmuHwProperties props,
                         mtrace_control_func_t* mtrace_control);
  ~PerfmonDevice() = default;

  const PmuHwProperties& pmu_hw_properties() const { return pmu_hw_properties_; }

  void DdkRelease();

  // Handlers for each of the operations.
  void PmuGetProperties(FidlPerfmonProperties* props);
  zx_status_t PmuInitialize(const FidlPerfmonAllocation* allocation);
  void PmuTerminate();
  zx_status_t PmuGetAllocation(FidlPerfmonAllocation* allocation);
  zx_status_t PmuGetBufferHandle(uint32_t descriptor, zx_handle_t* out_handle);
  zx_status_t PmuStageConfig(const FidlPerfmonConfig* config);
  zx_status_t PmuGetConfig(FidlPerfmonConfig* config);
  zx_status_t PmuStart();
  void PmuStop();

  // FIDL server methods
  void GetProperties(GetPropertiesRequestView request,
                     GetPropertiesCompleter::Sync& completer) override;
  void Initialize(InitializeRequestView request, InitializeCompleter::Sync& completer) override;
  void Terminate(TerminateRequestView request, TerminateCompleter::Sync& completer) override;
  void GetAllocation(GetAllocationRequestView request,
                     GetAllocationCompleter::Sync& completer) override;
  void StageConfig(StageConfigRequestView request, StageConfigCompleter::Sync& completer) override;
  void GetConfig(GetConfigRequestView request, GetConfigCompleter::Sync& completer) override;
  void GetBufferHandle(GetBufferHandleRequestView request,
                       GetBufferHandleCompleter::Sync& completer) override;
  void Start(StartRequestView request, StartCompleter::Sync& completer) override;
  void Stop(StopRequestView request, StopCompleter::Sync& completer) override;

  // Device protocol implementation
  zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags);
  zx_status_t DdkClose(uint32_t flags);
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);

 private:
  static void FreeBuffersForTrace(PmuPerTraceState* per_trace, uint32_t num_allocated);

  // Architecture-provided helpers for |PmuStageConfig()|.
  // Initialize |ss| in preparation for processing the PMU configuration.
  void InitializeStagingState(StagingState* ss);
  // Stage fixed counter |input_index| in |icfg|.
  zx_status_t StageFixedConfig(const FidlPerfmonConfig* icfg, StagingState* ss,
                               unsigned input_index, PmuConfig* ocfg);
  // Stage fixed counter |input_index| in |icfg|.
  zx_status_t StageProgrammableConfig(const FidlPerfmonConfig* icfg, StagingState* ss,
                                      unsigned input_index, PmuConfig* ocfg);
  // Stage fixed counter |input_index| in |icfg|.
  zx_status_t StageMiscConfig(const FidlPerfmonConfig* icfg, StagingState* ss, unsigned input_index,
                              PmuConfig* ocfg);
  // Verify the result. This is where the architecture can do any last
  // minute verification.
  zx_status_t VerifyStaging(StagingState* ss, PmuConfig* ocfg);
  // End of architecture-provided helpers.

  zx::bti bti_;

  // Properties of the PMU computed when the device driver is loaded.
  const PmuHwProperties pmu_hw_properties_;

  // The zx_mtrace_control() syscall to use. In the real device this is the syscall itself.
  // In tests it is replaced with something suitable for the test.
  mtrace_control_func_t* const mtrace_control_;

  mtx_t lock_{};

  // Only one open of this device is supported at a time. KISS for now.
  bool opened_ = false;

  // Once tracing has started various things are not allowed until it stops.
  bool active_ = false;

  // one entry for each trace
  // TODO(dje): At the moment we only support one trace at a time.
  // "trace" == "data collection run"
  std::unique_ptr<PmuPerTraceState> per_trace_state_;
};

}  // namespace perfmon

#endif  // SRC_DEVICES_MISC_DRIVERS_CPU_TRACE_PERF_MON_H_
