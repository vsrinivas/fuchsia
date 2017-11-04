// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CPUPERF_PROVIDER_IMPORTER_H_
#define GARNET_BIN_CPUPERF_PROVIDER_IMPORTER_H_

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include <trace-engine/context.h>
#include <zircon/device/intel-pm.h>

#include "garnet/bin/cpuperf_provider/events.h"
#include "lib/fxl/macros.h"

namespace cpuperf_provider {

class Reader;

class Importer {
public:
  Importer(trace_context* context, uint32_t category_mask,
           trace_ticks_t start_time, trace_ticks_t stop_time);
  ~Importer();

  bool Import(Reader& reader);

private:
  static constexpr size_t kMaxNumCpus = 32;
  static_assert(kMaxNumCpus <= TRACE_ENCODED_THREAD_REF_MAX_INDEX,
                "bad value for kMaxNumCpus");

  uint64_t ImportTallyRecords(Reader& reader,
                              const zx_x86_ipm_state_t& state,
                              const zx_x86_ipm_perf_config_t& config);
  uint64_t ImportSampleRecords(Reader& reader,
                               const zx_x86_ipm_state_t& state,
                               const zx_x86_ipm_perf_config_t& config);

  void ImportTallyRecord(trace_cpu_number_t cpu,
                         const zx_x86_ipm_state_t& state,
                         const zx_x86_ipm_perf_config_t& config,
                         const zx_x86_ipm_counters_t& counters);
  void ImportProgrammableSampleRecord(trace_cpu_number_t cpu,
                                      const zx_x86_ipm_state_t& state,
                                      const zx_x86_ipm_perf_config_t& config,
                                      const zx_x86_ipm_sample_record_t& record,
                                      trace_ticks_t previous_time,
                                      uint64_t counter_value,
                                      uint64_t* programmable_counter_value);
  void ImportFixedSampleRecord(trace_cpu_number_t cpu,
                               const zx_x86_ipm_state_t& state,
                               const zx_x86_ipm_perf_config_t& config,
                               const zx_x86_ipm_sample_record_t& record,
                               trace_ticks_t previous_time,
                               uint64_t counter_value,
                               uint64_t* fixed_counter_value);

  void EmitTallyRecord(trace_cpu_number_t cpu, const EventDetails* details,
                       const trace_string_ref_t& category_ref,
                       uint64_t value);

  void EmitSampleRecord(trace_cpu_number_t cpu, const EventDetails* details,
                        const trace_string_ref_t& category_ref,
                        trace_ticks_t start_time, trace_ticks_t end_time,
                        uint64_t value);

  trace_thread_ref_t GetCpuThreadRef(trace_cpu_number_t cpu);

  bool IsSampleMode() const;
  bool IsTallyMode() const { return !IsSampleMode(); }

  trace_context* const context_;
  uint32_t category_mask_;
  trace_ticks_t start_time_;
  trace_ticks_t stop_time_;

  trace_string_ref_t const cpu_string_ref_;
  trace_string_ref_t const fixed_category_ref_;
  trace_string_ref_t const value_name_ref_;
  trace_string_ref_t const rate_name_ref_;

  trace_thread_ref_t cpu_thread_refs_[kMaxNumCpus];

  FXL_DISALLOW_COPY_AND_ASSIGN(Importer);
};

}  // namespace cpuperf_provider

#endif  // GARNET_BIN_CPUPERF_PROVIDER_IMPORTER_H_
