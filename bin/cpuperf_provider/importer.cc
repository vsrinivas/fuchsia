// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cpuperf_provider/importer.h"

#include <inttypes.h>
#include <atomic>

#include <fbl/algorithm.h>
#include <zircon/syscalls.h>

#include "garnet/bin/cpuperf_provider/categories.h"
#include "garnet/lib/cpuperf/reader.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/fxl/time/time_point.h"

namespace cpuperf_provider {

// Mock process for cpus. The infrastructure only supports processes and
// threads.
constexpr zx_koid_t kCpuProcess = 1u;

Importer::Importer(trace_context_t* context, const TraceConfig* trace_config,
                   trace_ticks_t start_time, trace_ticks_t stop_time)
#define MAKE_STRING(literal) \
  trace_context_make_registered_string_literal(context, literal)
    : context_(context),
      trace_config_(trace_config),
      start_time_(start_time),
      stop_time_(stop_time),
      cpu_string_ref_(MAKE_STRING("cpu")),
      cpuperf_category_ref_(MAKE_STRING("cpu:perf")),
      count_name_ref_(MAKE_STRING("count")),
      value_name_ref_(MAKE_STRING("value")),
      rate_name_ref_(MAKE_STRING("rate")),
      aspace_name_ref_(MAKE_STRING("aspace")),
      pc_name_ref_(MAKE_STRING("pc")) {
  for (unsigned cpu = 0; cpu < countof(cpu_thread_refs_); ++cpu) {
    // +1 because index thread refs start at 1
    trace_thread_index_t index = cpu + 1;
    cpu_thread_refs_[cpu] = trace_make_indexed_thread_ref(index);
    // Note: Thread ids of zero are invalid. We use "thread 0" (aka cpu 0)
    // for system-wide events.
    trace_context_write_thread_record(context, index, kCpuProcess, cpu);
    if (cpu == 0) {
      cpu_name_refs_[0] =
          trace_context_make_registered_string_literal(context, "system");
    } else {
      const char* name = fxl::StringPrintf("cpu%u", cpu - 1).c_str();
      cpu_name_refs_[cpu] = trace_context_make_registered_string_copy(
          context, name, strlen(name));
    }
    // TODO(dje): In time emit "cpuN" for thread names, but it won't do any
    // good at the moment as we use "Count" records which ignore the thread.
  }
#undef MAKE_STRING
}

Importer::~Importer() = default;

bool Importer::Import(cpuperf::Reader& reader) {
  trace_context_write_process_info_record(context_, kCpuProcess,
                                          &cpu_string_ref_);

  auto start = fxl::TimePoint::Now();

  cpuperf_properties_t props;
  if (!reader.GetProperties(&props)) {
    FXL_LOG(ERROR) << "Error reading CPU performance properties";
    return false;
  }

  cpuperf_config_t config;
  if (!reader.GetConfig(&config)) {
    FXL_LOG(ERROR) << "Error reading CPU performance config";
    return false;
  }

  uint32_t record_count = ImportRecords(reader, props, config);

  FXL_LOG(INFO) << "Import of " << record_count << " cpu perf records took "
                << (fxl::TimePoint::Now() - start).ToMicroseconds() << " us";

  return true;
}

uint64_t Importer::ImportRecords(cpuperf::Reader& reader,
                                 const cpuperf_properties_t& props,
                                 const cpuperf_config_t& config) {
  EventTracker event_data(start_time_);
  uint32_t record_count = 0;
  // Only print these warnings once, and then again at the end with
  // the total count. We don't want a broken trace to flood the screen
  // with messages.
  uint32_t printed_zero_period_warning_count = 0;
  uint32_t printed_old_time_warning_count = 0;
  uint32_t printed_late_record_warning_count = 0;

  uint32_t cpu;
  cpuperf::Reader::SampleRecord record;

  uint64_t sample_rate = trace_config_->sample_rate();
  bool is_tally_mode = sample_rate == 0;
  trace_ticks_t current_time = reader.time();

  while (reader.ReadNextRecord(&cpu, &record)) {
    FXL_DCHECK(cpu < kMaxNumCpus);
    cpuperf_event_id_t event_id = record.event();
    uint64_t ticks_per_second = reader.ticks_per_second();

    // There can be millions of records. This log message is useful for small
    // test runs, but otherwise is too painful. The verbosity level is chosen
    // to recognize that.
    FXL_VLOG(10) << fxl::StringPrintf(
        "Import: cpu=%u, event=0x%x, time=%" PRIu64, cpu, event_id,
        current_time);

    if (record.type() == CPUPERF_RECORD_TIME) {
      current_time = reader.time();
      if (event_id == CPUPERF_EVENT_ID_NONE) {
        // This is just a time update, not a combined time+tick record.
        ++record_count;
        continue;
      }
    }

    // Get the time we last saw this event.
    trace_ticks_t prev_time = event_data.GetTime(cpu, event_id);

    if (current_time < prev_time) {
      if (printed_old_time_warning_count == 0) {
        FXL_LOG(WARNING) << "cpu " << cpu << ": record time " << current_time
                         << " < previous time " << prev_time
                         << " (further such warnings are omitted)";
      }
      ++printed_old_time_warning_count;
    } else if (current_time == prev_time) {
      if (printed_zero_period_warning_count == 0) {
        FXL_LOG(WARNING) << "cpu " << cpu << ": empty interval at time "
                         << current_time
                         << " (further such warnings are omitted)";
      }
      ++printed_zero_period_warning_count;
    } else if (current_time > stop_time_) {
      if (printed_late_record_warning_count == 0) {
        FXL_LOG(WARNING) << "Record has time > stop_time: " << current_time
                         << " (further such warnings are omitted)";
      }
      ++printed_late_record_warning_count;
    } else {
      switch (record.type()) {
        case CPUPERF_RECORD_TIME:
          FXL_DCHECK(event_id != CPUPERF_EVENT_ID_NONE);
          // fall through
        case CPUPERF_RECORD_TICK:
          if (is_tally_mode) {
            event_data.AccumulateCount(cpu, event_id, sample_rate);
          } else {
            ImportSampleRecord(cpu, config, record, prev_time, current_time,
                               ticks_per_second, sample_rate);
          }
          break;
        case CPUPERF_RECORD_COUNT:
          if (is_tally_mode) {
            event_data.AccumulateCount(cpu, event_id, record.count->count);
          } else {
            ImportSampleRecord(cpu, config, record, prev_time, current_time,
                               ticks_per_second, record.count->count);
          }
          break;
        case CPUPERF_RECORD_VALUE:
          if (is_tally_mode) {
            event_data.UpdateValue(cpu, event_id, record.value->value);
          } else {
            ImportSampleRecord(cpu, config, record, prev_time, current_time,
                               ticks_per_second, record.value->value);
          }
          break;
        case CPUPERF_RECORD_PC:
          if (!is_tally_mode) {
            ImportSampleRecord(cpu, config, record, prev_time, current_time,
                               ticks_per_second, sample_rate);
          }
          break;
        default:
          // The reader shouldn't be returning unknown records.
          FXL_NOTREACHED();
          break;
      }
    }

    event_data.UpdateTime(cpu, event_id, current_time);
    ++record_count;
  }

  if (is_tally_mode) {
    EmitTallyCounts(config, &event_data);
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

void Importer::ImportSampleRecord(trace_cpu_number_t cpu,
                                  const cpuperf_config_t& config,
                                  const cpuperf::Reader::SampleRecord& record,
                                  trace_ticks_t previous_time,
                                  trace_ticks_t current_time,
                                  uint64_t ticks_per_second,
                                  uint64_t event_value) {
  cpuperf_event_id_t event_id = record.event();
  const cpuperf::EventDetails* details;
  // Note: Errors here are generally rare, so at present we don't get clever
  // with minimizing the noise.
  if (cpuperf::EventIdToEventDetails(event_id, &details)) {
    EmitSampleRecord(cpu, details, record, previous_time, current_time,
                     ticks_per_second, event_value);
  } else {
    FXL_LOG(ERROR) << "Invalid event id: " << event_id;
  }
}

void Importer::EmitSampleRecord(trace_cpu_number_t cpu,
                                const cpuperf::EventDetails* details,
                                const cpuperf::Reader::SampleRecord& record,
                                trace_ticks_t start_time,
                                trace_ticks_t end_time,
                                uint64_t ticks_per_second, uint64_t value) {
  FXL_DCHECK(start_time < end_time);
  trace_thread_ref_t thread_ref{GetCpuThreadRef(cpu, details->id)};
  trace_string_ref_t name_ref{
      trace_context_make_registered_string_literal(context_, details->name)};
#if 0
  // Count records are "process wide" so we need some way to distinguish
  // each cpu. Thus while it might be nice to use this for "id" we don't.
  uint64_t id = record.event();
#else
  // Add one as zero doesn't get printed.
  uint64_t id = cpu + 1;
#endif

  // While the count of events is cumulative, it's more useful to report some
  // measure that's useful within each time period. E.g., a rate.
  uint64_t interval_ticks = end_time - start_time;
  FXL_DCHECK(interval_ticks > 0);
  double rate_per_second = 0;
  // rate_per_second = value / (interval_ticks / ticks_per_second)
  // ticks_per_second could be zero if there's bad data in the buffer.
  // Don't crash because of it. If it's zero just punt and compute the rate
  // per tick.
  // TODO(dje): Perhaps the rate calculation should be done in the report
  // generator, but it's done this way so that catapult reports in chrome
  // are usable. Maybe add a new phase type to the catapult format?
  rate_per_second = static_cast<double>(value) / interval_ticks;
  if (ticks_per_second != 0) {
    rate_per_second *= ticks_per_second;
  }

  trace_arg_t args[3];
  size_t n_args;
  switch (record.type()) {
    case CPUPERF_RECORD_TICK:
      args[0] = {trace_make_arg(rate_name_ref_,
                                trace_make_double_arg_value(rate_per_second))};
      n_args = 1;
      break;
    case CPUPERF_RECORD_COUNT:
      args[0] = {trace_make_arg(rate_name_ref_,
                                trace_make_double_arg_value(rate_per_second))};
      n_args = 1;
      break;
    case CPUPERF_RECORD_VALUE:
      // We somehow need to mark the value as not being a count. This is
      // important for some consumers to guide how to print the value.
      // Do this by using a different name for the value.
      args[0] = {
          trace_make_arg(value_name_ref_, trace_make_uint64_arg_value(value))};
      n_args = 1;
      break;
    case CPUPERF_RECORD_PC:
      args[1] = {trace_make_arg(
          aspace_name_ref_, trace_make_uint64_arg_value(record.pc->aspace))};
      args[2] = {trace_make_arg(pc_name_ref_,
                                trace_make_uint64_arg_value(record.pc->pc))};
      n_args = 3;
      break;
    default:
      FXL_NOTREACHED();
      return;
  }

#if 0
  // TODO(dje): This is a failed experiment to use something other than
  // counters. It is kept, for now, to allow easy further experimentation.
  trace_context_write_async_begin_event_record(
      context_, start_time,
      &thread_ref, &cpuperf_category_ref_, &name_ref,
      id, nullptr, 0);
  trace_context_write_async_end_event_record(
      context_, end_time,
      &thread_ref, &cpuperf_category_ref_, &name_ref,
      id, &args[0], n_args);
#else
  // Chrome interprets the timestamp we give it as the start of the
  // interval, which for a count makes sense: this is the value of the count
  // from this point on until the next count record. We're abusing this record
  // type to display a rate.
  trace_context_write_counter_event_record(context_, start_time, &thread_ref,
                                           &cpuperf_category_ref_, &name_ref,
                                           id, &args[0], n_args);
#endif
}

// Chrome interprets the timestamp we give Count records as the start
// of the interval with that count, which for a count makes sense: this is
// the value of the count from this point on until the next count record.
// But if we emit a value of zero at the start (or don't emit any initial
// value at all) Chrome shows the entire trace of having the value zero and
// the count record at the end of the interval is very hard to see.
// OTOH the data is correct, it's just the display that's hard to read.
// Text display of the results is unaffected.
// One important reason for providing a value at the start is because there's
// currently no other way to communicate the start time of the trace in a
// json output file, and thus there would otherwise be no way for the
// report printer to know the duration over which the count was collected.
void Importer::EmitTallyCounts(const cpuperf_config_t& config,
                               const EventTracker* event_data) {
  unsigned num_cpus = zx_system_get_num_cpus();

  for (unsigned cpu = 0; cpu < num_cpus; ++cpu) {
    for (unsigned ctr = 0; ctr < countof(config.events) &&
                           config.events[ctr] != CPUPERF_EVENT_ID_NONE;
         ++ctr) {
      cpuperf_event_id_t event_id = config.events[ctr];
      if (event_data->HaveValue(cpu, event_id)) {
        uint64_t value = event_data->GetCountOrValue(cpu, event_id);
        if (event_data->IsValue(cpu, event_id)) {
          EmitTallyRecord(cpu, event_id, stop_time_, true, value);
        } else {
          EmitTallyRecord(cpu, event_id, start_time_, false, 0);
          EmitTallyRecord(cpu, event_id, stop_time_, false, value);
        }
      }
    }
  }
}

void Importer::EmitTallyRecord(trace_cpu_number_t cpu,
                               cpuperf_event_id_t event_id, trace_ticks_t time,
                               bool is_value, uint64_t value) {
  trace_thread_ref_t thread_ref{GetCpuThreadRef(cpu, event_id)};
  trace_arg_t args[1] = {
      {trace_make_arg(is_value ? value_name_ref_ : count_name_ref_,
                      trace_make_uint64_arg_value(value))},
  };
  const cpuperf::EventDetails* details;
  if (cpuperf::EventIdToEventDetails(event_id, &details)) {
    trace_string_ref_t name_ref{
        trace_context_make_registered_string_literal(context_, details->name)};
    trace_context_write_counter_event_record(context_, time, &thread_ref,
                                             &cpuperf_category_ref_, &name_ref,
                                             event_id, &args[0], countof(args));
  } else {
    FXL_LOG(WARNING) << "Invalid event id: " << event_id;
  }
}

trace_string_ref_t Importer::GetCpuNameRef(trace_cpu_number_t cpu) {
  FXL_DCHECK(cpu < countof(cpu_name_refs_));
  return cpu_name_refs_[cpu + 1];
}

trace_thread_ref_t Importer::GetCpuThreadRef(trace_cpu_number_t cpu,
                                             cpuperf_event_id_t id) {
  FXL_DCHECK(cpu < countof(cpu_thread_refs_));
  // TODO(dje): Misc events are currently all system-wide, not attached
  // to any specific cpu. That won't always be the case.
  if (CPUPERF_EVENT_ID_UNIT(id) == CPUPERF_UNIT_MISC)
    cpu = 0;
  else
    ++cpu;
  return cpu_thread_refs_[cpu];
}

}  // namespace cpuperf_provider
