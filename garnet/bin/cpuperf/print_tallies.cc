// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <limits>
#include <locale>
#include <sstream>
#include <unordered_map>
#include <vector>

#include <src/lib/fxl/command_line.h>
#include "src/lib/files/file.h"
#include <src/lib/fxl/log_settings.h>
#include <src/lib/fxl/log_settings_command_line.h>
#include <src/lib/fxl/logging.h>

#include <lib/zircon-internal/device/cpu-trace/perf-mon.h>
#include <zircon/syscalls.h>

#include "garnet/lib/debugger_utils/util.h"
#include "garnet/lib/perfmon/controller.h"
#include "garnet/lib/perfmon/events.h"

#include "session_spec.h"
#include "session_result_spec.h"

// Allow space for 999,999,999.
// This is |int| as it is used as the width arg to fprintf.
constexpr int kMinColumnWidth = 11;

// Width of the first column, which is trace names.
// This is |int| as it is used as the width arg to fprintf.
constexpr int kTraceNameColumnWidth = sizeof("Trace NNN:") - 1;

struct EventColumn {
  const char* name;
  int width;
};

using SessionColumns = std::unordered_map<perfmon_event_id_t, EventColumn>;

struct EventResult {
  uint64_t value_or_count;
};

using TraceResults = std::unordered_map<perfmon_event_id_t, EventResult>;

// Indexed by trace number.
using SessionResults = std::vector<TraceResults>;

template<typename T>
void IterateOverEventIds(const cpuperf::SessionSpec& spec,
                         const perfmon::ModelEventManager* model_event_manager,
                         T func) {
  for (size_t i = 0; i < PERFMON_MAX_EVENTS; ++i) {
    perfmon_event_id_t id = spec.perfmon_config.events[i];
    if (id == 0) {
      // End of present events.
      break;
    }

    const perfmon::EventDetails* details = nullptr;
    if (!model_event_manager->EventIdToEventDetails(id, &details)) {
      // This shouldn't happen, but let |func| decide what to do.
    }

    func(i, id, details);
  }
}

static SessionColumns BuildSessionColumns(
    const cpuperf::SessionSpec& spec,
    const perfmon::ModelEventManager* model_event_manager) {
  SessionColumns columns;

  IterateOverEventIds(spec, model_event_manager,
                      [&columns] (size_t index, perfmon_event_id_t id,
                                  const perfmon::EventDetails* details) {
    const char* name;
    if (details) {
      name = details->name;
    } else {
      // This shouldn't happen, but better to print what we have.
      name = "Unknown";
    }
    size_t len = strlen(name);
    FXL_DCHECK(len <= std::numeric_limits<int>::max());
    int int_len = static_cast<int>(len);
    int width = std::max(int_len, kMinColumnWidth);

    columns[id] = EventColumn{ .name = name, .width = width };
  });

  return columns;
}

// Data is printed in the order it appears in |spec|.
static void PrintColumnTitles(FILE* f, const cpuperf::SessionSpec& spec,
                              const perfmon::ModelEventManager* model_event_manager,
                              const SessionColumns& columns) {
  fprintf(f, "%*s", kTraceNameColumnWidth, "");

  IterateOverEventIds(spec, model_event_manager,
                      [&] (size_t index, perfmon_event_id_t id,
                           const perfmon::EventDetails* details) {
    auto iter = columns.find(id);
    FXL_DCHECK(iter != columns.end());
    const EventColumn& column = iter->second;
    fprintf(f, "|%*s", column.width, column.name);
  });

  fprintf(f, "\n");
}

static void PrintTrace(FILE* f, const cpuperf::SessionSpec& spec,
                       const cpuperf::SessionResultSpec& result_spec,
                       const perfmon::ModelEventManager* model_event_manager,
                       const SessionColumns& columns, uint32_t trace_num,
                       const TraceResults& results) {
  char label[kTraceNameColumnWidth + 1];
  snprintf(&label[0], sizeof(label), "Trace %u:", trace_num);
  fprintf(f, "%-*s", kTraceNameColumnWidth, label);

  IterateOverEventIds(spec, model_event_manager,
                      [&] (size_t index, perfmon_event_id_t id,
                           const perfmon::EventDetails* details) {
    auto column_iter = columns.find(id);
    FXL_DCHECK(column_iter != columns.end());
    const EventColumn& column = column_iter->second;
    auto result_iter = results.find(id);
    if (result_iter != results.end()) {
      const EventResult& result = result_iter->second;
      std::stringstream ss;
      // Print 123456 as 123,456 (locale appropriate).
      struct threes : std::numpunct<char> {
        std::string do_grouping() const { return "\3"; }
      };
      ss.imbue(std::locale(ss.getloc(), new threes));
      ss << result.value_or_count;
      fprintf(f, "|%*s", column.width, ss.str().c_str());
    } else {
      // Misc events might not be present in all traces.
      // Just print blanks.
      // TODO(dje): Distinguish such properties in EventDetails?
      fprintf(f, "|%*s", column.width, "");
    }
  });

  fprintf(f, "\n");
}

void PrintTallyResults(FILE* f, const cpuperf::SessionSpec& spec,
                       const cpuperf::SessionResultSpec& result_spec,
                       const perfmon::ModelEventManager* model_event_manager,
                       perfmon::Controller* controller) {
  std::unique_ptr<perfmon::DeviceReader> reader = controller->GetReader();
  if (!reader) {
    return;
  }

  SessionColumns columns = BuildSessionColumns(spec, model_event_manager);

  SessionResults results;
  for (size_t i = 0; i < result_spec.num_traces; ++i) {
    results.emplace_back(TraceResults{});
  }

  constexpr uint32_t kCurrentTraceUnset = ~0;
  uint32_t current_trace = kCurrentTraceUnset;

  uint32_t trace;
  perfmon::SampleRecord record;
  perfmon::ReaderStatus status;
  while ((status = reader->ReadNextRecord(&trace, &record)) ==
         perfmon::ReaderStatus::kOk) {
    if (trace != current_trace) {
      current_trace = trace;
    }

    if (record.header->event == 0)
      continue;
    perfmon_event_id_t id = record.header->event;
    const perfmon::EventDetails* details;
    if (!model_event_manager->EventIdToEventDetails(id, &details)) {
      FXL_LOG(WARNING) << "Unknown event: 0x" << std::hex
                       << record.header->event;
      continue;
    }

    switch (record.type()) {
    case PERFMON_RECORD_COUNT:
      results[current_trace][id] = EventResult{record.count->count};
      break;
    case PERFMON_RECORD_VALUE:
      results[current_trace][id] = EventResult{record.value->value};
      break;
    default:
      break;
    }
  }

  PrintColumnTitles(f, spec, model_event_manager, columns);

  for (uint32_t i = 0; i < result_spec.num_traces; ++i) {
    PrintTrace(f, spec, result_spec, model_event_manager, columns, i,
               results[i]);
  }
}
