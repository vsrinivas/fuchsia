// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CPUPERF_PROVIDER_IMPORTER_H_
#define GARNET_BIN_CPUPERF_PROVIDER_IMPORTER_H_

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include <unordered_map>

#include <trace-engine/context.h>
#include <zircon/device/cpu-trace/cpu-perf.h>

#include "garnet/bin/cpuperf_provider/categories.h"
#include "garnet/lib/cpuperf/events.h"
#include "garnet/lib/cpuperf/reader.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace cpuperf_provider {

class Importer {
 public:
  Importer(trace_context* context, const TraceConfig* trace_config,
           trace_ticks_t start_time, trace_ticks_t stop_time);
  ~Importer();

  bool Import(cpuperf::Reader& reader);

 private:
  static constexpr size_t kMaxNumCpus = 32;
  static_assert(kMaxNumCpus <= TRACE_ENCODED_THREAD_REF_MAX_INDEX,
                "bad value for kMaxNumCpus");

  class EventTracker final {
   public:
    EventTracker(trace_ticks_t start_time) : start_time_(start_time) {}

    bool HaveValue(unsigned cpu, cpuperf_event_id_t id) const {
      Key key = GenKey(cpu, id);
      EventData::const_iterator iter = data_.find(key);
      return iter != data_.end();
    }

    void UpdateTime(unsigned cpu, cpuperf_event_id_t id, trace_ticks_t time) {
      Key key = GenKey(cpu, id);
      data_[key].time = time;
    }

    trace_ticks_t GetTime(unsigned cpu, cpuperf_event_id_t id) const {
      Key key = GenKey(cpu, id);
      EventData::const_iterator iter = data_.find(key);
      if (iter == data_.end())
        return start_time_;
      return iter->second.time;
    }

    void UpdateValue(unsigned cpu, cpuperf_event_id_t id, uint64_t value) {
      Key key = GenKey(cpu, id);
      data_[key].is_value = true;
      data_[key].count_or_value = value;
    }

    void AccumulateCount(unsigned cpu, cpuperf_event_id_t id, uint64_t value) {
      Key key = GenKey(cpu, id);
      data_[key].is_value = false;
      data_[key].count_or_value += value;
    }

    bool IsValue(unsigned cpu, cpuperf_event_id_t id) const {
      Key key = GenKey(cpu, id);
      EventData::const_iterator iter = data_.find(key);
      FXL_DCHECK(iter != data_.end());
      return iter->second.is_value;
    }

    uint64_t GetCountOrValue(unsigned cpu, cpuperf_event_id_t id) const {
      Key key = GenKey(cpu, id);
      EventData::const_iterator iter = data_.find(key);
      if (iter == data_.end())
        return 0;
      return iter->second.count_or_value;
    }

   private:
    using Key = uint32_t;
    struct Data {
      trace_ticks_t time = 0;
      // false -> count (CPUPERF_RECORD_COUNT),
      // true -> value (CPUPERF_RECORD_VALUE).
      bool is_value;
      // This is either a count or a value.
      // Records for any particular event should only be using one
      // of CPUPERF_RECORD_{COUNT,VALUE}.
      uint64_t count_or_value = 0;
    };
    using EventData = std::unordered_map<Key, Data>;

    Key GenKey(unsigned cpu, cpuperf_event_id_t id) const {
      FXL_DCHECK(cpu < kMaxNumCpus);
      static_assert(sizeof(id) == 2, "");
      return (cpu << 16) | id;
    }

    const trace_ticks_t start_time_;
    EventData data_;
  };

  uint64_t ImportRecords(cpuperf::Reader& reader,
                         const cpuperf_properties_t& props,
                         const cpuperf_config_t& config);

  void ImportSampleRecord(trace_cpu_number_t cpu,
                          const cpuperf_config_t& config,
                          const cpuperf::Reader::SampleRecord& record,
                          trace_ticks_t previous_time,
                          trace_ticks_t current_time, uint64_t ticks_per_second,
                          uint64_t event_value);

  void EmitSampleRecord(trace_cpu_number_t cpu,
                        const cpuperf::EventDetails* details,
                        const cpuperf::Reader::SampleRecord& record,
                        trace_ticks_t start_time, trace_ticks_t end_time,
                        uint64_t ticks_per_second, uint64_t value);

  void EmitTallyCounts(const cpuperf_config_t& config,
                       const EventTracker* event_data);

  void EmitTallyRecord(trace_cpu_number_t cpu, cpuperf_event_id_t event_id,
                       trace_ticks_t time, bool is_value, uint64_t value);

  trace_string_ref_t GetCpuNameRef(trace_cpu_number_t cpu);

  trace_thread_ref_t GetCpuThreadRef(trace_cpu_number_t cpu,
                                     cpuperf_event_id_t id);

  trace_context* const context_;
  const TraceConfig* trace_config_;
  trace_ticks_t start_time_;
  trace_ticks_t stop_time_;

  trace_string_ref_t const cpu_string_ref_;
  // Our use of the "category" argument to trace_context_write_* functions
  // is a bit abnormal. The argument "should" be the name of the category
  // the user provided. However, users can select individual events or
  // collections of events and the mapping from user-provided category
  // name to our output is problematic. So just use a single category to
  // encompass all of them ("cpu:perf") and use the name argument to
  // identify each event.
  trace_string_ref_t const cpuperf_category_ref_;
  trace_string_ref_t const count_name_ref_;
  trace_string_ref_t const value_name_ref_;
  trace_string_ref_t const rate_name_ref_;
  trace_string_ref_t const aspace_name_ref_;
  trace_string_ref_t const pc_name_ref_;

  // Add one for events that are system-wide (e.g., memory controller events).
  trace_thread_ref_t cpu_thread_refs_[kMaxNumCpus + 1];

  // Add one for events that are system-wide (e.g., memory controller events).
  trace_string_ref_t cpu_name_refs_[kMaxNumCpus + 1];

  FXL_DISALLOW_COPY_AND_ASSIGN(Importer);
};

}  // namespace cpuperf_provider

#endif  // GARNET_BIN_CPUPERF_PROVIDER_IMPORTER_H_
