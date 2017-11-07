// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(dje): IWBN to have this available a separate library and dumper.

#include "garnet/bin/cpuperf_provider/importer.h"

#include <atomic>
#include <inttypes.h>

#include <zircon/syscalls.h>
#include <fbl/algorithm.h>

#include "garnet/bin/cpuperf_provider/categories.h"
#include "garnet/bin/cpuperf_provider/reader.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/fxl/time/time_point.h"

namespace cpuperf_provider {

namespace {

// Mock process for cpus. The infrastructure only supports processes and
// threads.
constexpr zx_koid_t kCpuProcess = 1u;

}  // namespace

Importer::Importer(trace_context_t* context, uint32_t category_mask,
                   trace_ticks_t start_time, trace_ticks_t stop_time)
#define MAKE_STRING(literal) \
  trace_context_make_registered_string_literal(context, literal)
    : context_(context),
      category_mask_(category_mask),
      start_time_(start_time),
      stop_time_(stop_time),
      cpu_string_ref_(MAKE_STRING("cpu")),
      fixed_category_ref_(MAKE_STRING("cpu:fixed")),
      value_name_ref_(MAKE_STRING("value")),
      rate_name_ref_(MAKE_STRING("rate")) {
  for (unsigned cpu = 0; cpu < countof(cpu_thread_refs_); ++cpu) {
    // +1 because index thread refs start at 1
    trace_thread_index_t index = cpu + 1;
    cpu_thread_refs_[cpu] = trace_make_indexed_thread_ref(index);
    // +1 because thread ids of zero is invalid
    trace_context_write_thread_record(context, index, kCpuProcess, cpu + 1);
  }
#undef MAKE_STRING
}

Importer::~Importer() = default;

bool Importer::Import(Reader& reader) {
  trace_context_write_process_info_record(context_, kCpuProcess,
                                          &cpu_string_ref_);

  auto start = fxl::TimePoint::Now();

  zx_x86_ipm_state_t state;
  if (!reader.ReadState(&state)) {
    FXL_LOG(ERROR) << "Error reading CPU performance state";
    return false;
  }

  zx_x86_ipm_perf_config_t config;
  if (!reader.ReadPerfConfig(&config)) {
    FXL_LOG(ERROR) << "Error reading CPU performance config";
    return false;
  }

  FXL_VLOG(2) << fxl::StringPrintf("Beginning import: category_mask=0x%x",
                                   category_mask_);

  uint32_t record_count;
  if (IsSampleMode()) {
    record_count = ImportSampleRecords(reader, state, config);
  } else {
    record_count = ImportTallyRecords(reader, state, config);
  }

  FXL_LOG(INFO) << "Import of " << record_count << " cpu perf records took "
                << (fxl::TimePoint::Now() - start).ToMicroseconds() << " us";

  return true;
}

uint64_t Importer::ImportTallyRecords(
    Reader& reader,
    const zx_x86_ipm_state_t& state,
    const zx_x86_ipm_perf_config_t& config) {
  uint64_t record_count = 0;
  uint32_t cpu;
  zx_x86_ipm_counters_t counters;

  while (reader.ReadNextRecord(&cpu, &counters)) {
    ImportTallyRecord(cpu, state, config, counters);
    ++record_count;
  }

  return record_count;
}

uint64_t Importer::ImportSampleRecords(
    Reader& reader,
    const zx_x86_ipm_state_t& state,
    const zx_x86_ipm_perf_config_t& config) {
  trace_ticks_t programmable_previous_time[kMaxNumCpus][IPM_MAX_PROGRAMMABLE_COUNTERS];
  trace_ticks_t fixed_previous_time[kMaxNumCpus][IPM_MAX_FIXED_COUNTERS];
  uint64_t accum_programmable_counter_value[kMaxNumCpus][IPM_MAX_PROGRAMMABLE_COUNTERS] = {};
  uint64_t accum_fixed_counter_value[kMaxNumCpus][IPM_MAX_FIXED_COUNTERS] = {};
  for (size_t i = 0; i < kMaxNumCpus; ++i) {
    for (size_t j = 0; j < IPM_MAX_PROGRAMMABLE_COUNTERS; ++j) {
      programmable_previous_time[i][j] = start_time_;
    }
    for (size_t j = 0; j < IPM_MAX_FIXED_COUNTERS; ++j) {
      fixed_previous_time[i][j] = start_time_;
    }
  }

  uint32_t record_count = 0;
  // Only print these warnings once, and then again at the end with
  // the total count. We don't want a broken trace to flood the screen
  // with messages.
  uint32_t printed_zero_period_warning_count = 0;
  uint32_t printed_old_time_warning_count = 0;
  uint32_t printed_late_record_warning_count = 0;

  uint32_t cpu;
  zx_x86_ipm_sample_record_t record;

  uint64_t sample_freq = GetSampleFreq(category_mask_);
  FXL_DCHECK(sample_freq > 0);

  while (reader.ReadNextRecord(&cpu, &record)) {
    FXL_DCHECK(cpu < kMaxNumCpus);
    uint32_t counter = record.counter & ~IPM_COUNTER_NUMBER_FIXED;
    bool is_fixed = !!(record.counter & IPM_COUNTER_NUMBER_FIXED);
    trace_ticks_t prev_time;
    if (is_fixed) {
      prev_time = fixed_previous_time[cpu][counter];
    } else {
      prev_time = programmable_previous_time[cpu][counter];
    }

    FXL_VLOG(2) << fxl::StringPrintf("Import: cpu=%u, counter=0x%x, time=%"
                                     PRIu64,
                                     cpu, record.counter, record.time);

    if (record.time < prev_time) {
      if (printed_old_time_warning_count == 0) {
        FXL_LOG(WARNING) << "cpu " << cpu << ": record time " << record.time
                         << " < previous time " << prev_time
                         << " (further such warnings are omitted)";
      }
      ++printed_old_time_warning_count;
    } else if (record.time == prev_time) {
      if (printed_zero_period_warning_count == 0) {
        FXL_LOG(WARNING) << "cpu " << cpu
                         << ": empty interval at time " << record.time
                         << " (further such warnings are omitted)";
      }
      ++printed_zero_period_warning_count;
    } else if (record.time > stop_time_) {
      if (printed_late_record_warning_count == 0) {
        FXL_LOG(WARNING) << "Record has time > stop_time: " << record.time
                         << " (further such warnings are omitted)";
      }
      ++printed_late_record_warning_count;
    } else {
      if (is_fixed) {
        ImportFixedSampleRecord(
          cpu, state, config, record, prev_time, sample_freq,
          &accum_programmable_counter_value[cpu][counter]);
      } else {
        ImportProgrammableSampleRecord(
          cpu, state, config, record, prev_time, sample_freq,
          &accum_fixed_counter_value[cpu][counter]);
      }
    }

    if (is_fixed) {
      fixed_previous_time[cpu][counter] = record.time;
    } else {
      programmable_previous_time[cpu][counter] = record.time;
    }
    ++record_count;
  }

  if (printed_old_time_warning_count > 0) {
    FXL_LOG(WARNING) << printed_old_time_warning_count
                     << " total occurrences of records going back in time";
  }
  if (printed_zero_period_warning_count > 0) {
    FXL_LOG(WARNING) << printed_zero_period_warning_count
                     << " total occurrences of records with an empty interval";
  }
  if (printed_late_record_warning_count > 0) {
    FXL_LOG(WARNING) << printed_late_record_warning_count
                     << " total occurrences of records with late times";
  }

  return record_count;
}

void Importer::ImportTallyRecord(trace_cpu_number_t cpu,
                                 const zx_x86_ipm_state_t& state,
                                 const zx_x86_ipm_perf_config_t& config,
                                 const zx_x86_ipm_counters_t& counters) {
  FXL_VLOG(2) << fxl::StringPrintf("Import tally: cpu=%u", cpu);

  for (uint32_t i = 0; i < state.num_fixed_counters; ++i) {
    if (config.fixed_counter_ctrl & IA32_FIXED_CTR_CTRL_EN_MASK(i)) {
      FXL_VLOG(2) << fxl::StringPrintf("Import: fixed %u: %" PRIu64,
                                       i, counters.fixed_counters[i]);
      EmitTallyRecord(cpu, GetFixedEventDetails(i), fixed_category_ref_,
                      counters.fixed_counters[i]);
    }
  }

  uint32_t programmable_category = category_mask_ & IPM_CATEGORY_PROGRAMMABLE_MASK;
  if (programmable_category == IPM_CATEGORY_NONE)
    return;

  const char* category_name =
    GetProgrammableCategoryFromId(programmable_category).name;
  trace_string_ref_t category_ref = trace_context_make_registered_string_literal(context_, category_name);
  FXL_VLOG(2) << fxl::StringPrintf("Import: category name %s",
                                   category_name);
  for (uint32_t i = 0; i < state.num_programmable_counters; ++i) {
    if (!(config.programmable_events[i] & IA32_PERFEVTSEL_EN_MASK))
      continue;
    uint64_t event = config.programmable_events[i];
    uint64_t value = counters.programmable_counters[i];
    const EventDetails* details;
    if (EventSelectToEventDetails(event, &details)) {
      FXL_VLOG(2) << fxl::StringPrintf(
        "Import: event 0x%" PRIx64 "/%s: %" PRIu64,
        event, details->name, value);
      EmitTallyRecord(cpu, details, category_ref, value);
    } else {
      FXL_LOG(ERROR) << fxl::StringPrintf(
        "Unknown event in event select register %u: 0x%" PRIx64,
        i, event);
    }
  }
}

void Importer::ImportProgrammableSampleRecord(
    trace_cpu_number_t cpu, 
    const zx_x86_ipm_state_t& state,
    const zx_x86_ipm_perf_config_t& config,
    const zx_x86_ipm_sample_record_t& record,
    trace_ticks_t previous_time,
    uint64_t counter_value,
    uint64_t* accum_counter_value) {
  uint32_t counter = record.counter;
  // Note: Errors here are generally rare, so at present we don't get clever
  // with minimizing the noise.
  if (counter >= state.num_programmable_counters) {
    FXL_LOG(ERROR) << fxl::StringPrintf("Invalid programmable counter number: %u",
                                        counter);
    return;
  }
  *accum_counter_value += counter_value;

  uint32_t programmable_category = category_mask_ & IPM_CATEGORY_PROGRAMMABLE_MASK;
  if (programmable_category == IPM_CATEGORY_NONE) {
    FXL_LOG(ERROR) << "Got programmable counter record, but not enabled in category";
    return;
  }
  if (!(config.programmable_events[counter] & IA32_PERFEVTSEL_EN_MASK)) {
    FXL_LOG(ERROR) << "Got programmable counter record, but not enabled in event select";
    return;
  }
  const char* category_name =
    GetProgrammableCategoryFromId(programmable_category).name;
  trace_string_ref_t category_ref = trace_context_make_registered_string_literal(context_, category_name);

  uint64_t event = config.programmable_events[counter];
  const EventDetails* details;
  if (EventSelectToEventDetails(event, &details)) {
    FXL_VLOG(2) << fxl::StringPrintf("Import: event 0x%" PRIx64 "/%s",
                                     event, details->name);
    EmitSampleRecord(cpu, details, category_ref,
                     previous_time, record.time, counter_value);
  } else {
    FXL_LOG(ERROR) << fxl::StringPrintf(
      "Unknown event in event select register %u: 0x%" PRIx64,
      counter, event);
  }
}

void Importer::ImportFixedSampleRecord(
    trace_cpu_number_t cpu,
    const zx_x86_ipm_state_t& state,
    const zx_x86_ipm_perf_config_t& config,
    const zx_x86_ipm_sample_record_t& record,
    trace_ticks_t previous_time,
    uint64_t counter_value,
    uint64_t* accum_counter_value) {
  uint32_t counter = record.counter & ~IPM_COUNTER_NUMBER_FIXED;
  // Note: Errors here are generally rare, so at present we don't get clever
  // with minimizing the noise.
  if (counter < state.num_fixed_counters) {
    *accum_counter_value += counter_value;
    EmitSampleRecord(cpu, GetFixedEventDetails(counter), fixed_category_ref_,
                     previous_time, record.time, counter_value);
  } else {
    FXL_LOG(ERROR) << fxl::StringPrintf("Invalid fixed counter number: %u",
                                        counter);
  }
}

void Importer::EmitTallyRecord(trace_cpu_number_t cpu,
                               const EventDetails* details,
                               const trace_string_ref_t& category_ref,
                               uint64_t value) {
  trace_thread_ref_t thread_ref{GetCpuThreadRef(cpu)};
  trace_string_ref_t name_ref{trace_context_make_registered_string_literal(
      context_, details->name)};

  // TODO(dje): For now just show a value of 0 at the start and the
  // collected value at the end.

  trace_arg_t arg0{trace_make_arg(value_name_ref_, trace_make_uint64_arg_value(0))};
  trace_context_write_counter_event_record(
      context_, start_time_, &thread_ref, &category_ref, &name_ref,
      MakeEventKey(*details), &arg0, 1);

  trace_arg_t args[1] = {
    {trace_make_arg(value_name_ref_, trace_make_uint64_arg_value(value))},
  };
  trace_context_write_counter_event_record(
      context_, stop_time_, &thread_ref, &category_ref, &name_ref,
      MakeEventKey(*details), &args[0], countof(args));
}

void Importer::EmitSampleRecord(trace_cpu_number_t cpu,
                                const EventDetails* details,
                                const trace_string_ref_t& category_ref,
                                trace_ticks_t start_time,
                                trace_ticks_t end_time,
                                uint64_t value) {
  FXL_DCHECK(start_time < end_time);
  trace_thread_ref_t thread_ref{GetCpuThreadRef(cpu)};
  trace_string_ref_t name_ref{trace_context_make_registered_string_literal(
      context_, details->name)};
#if 0
  // Count records are "process wide" so we need some way to distinguish
  // each cpu. Thus while it might be nice to use this for "id" we don't.
  uint64_t id = MakeEventKey(*details);
#else
  // Add one as zero doesn't get printed.
  uint64_t id = cpu + 1;
#endif

#if 0
  trace_context_write_async_begin_event_record(
      context_, start_time,
      &thread_ref, &category_ref, &name_ref,
      id, nullptr, 0);

  trace_arg_t arg{trace_make_arg(value_name_ref_, trace_make_uint64_arg_value(value))};
  trace_context_write_async_end_event_record(
      context_, end_time,
      &thread_ref, &category_ref, &name_ref,
      id, &arg, 1);
#else
  // While the count of events is cumulative, it's more useful to report some
  // measure that's useful within each time period. E.g., a rate.
  uint64_t period = end_time - start_time;
  FXL_DCHECK(period > 0);
  double rate = 0;
  rate = static_cast<double>(value) / period;
  trace_arg_t args[1] = {
    {trace_make_arg(rate_name_ref_, trace_make_double_arg_value(rate))},
  };
  // TODO(dje): Chrome seems to take the timestamp we give it as the start
  // of the interval. Verify this is true.
  trace_context_write_counter_event_record(
      context_, start_time,
      &thread_ref, &category_ref, &name_ref,
      id, &args[0], countof(args));
#endif
}

trace_thread_ref_t Importer::GetCpuThreadRef(trace_cpu_number_t cpu) {
  FXL_DCHECK(cpu < countof(cpu_thread_refs_));
  return cpu_thread_refs_[cpu];
}

bool Importer::IsSampleMode() const {
  return (category_mask_ & IPM_CATEGORY_MODE_MASK) != 0;
}

}  // namespace cpuperf_provider
