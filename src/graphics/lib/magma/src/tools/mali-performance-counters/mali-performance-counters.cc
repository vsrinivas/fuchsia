// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mali-performance-counters.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/trace-provider/provider.h>
#include <lib/trace/event.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <stdlib.h>

#include <optional>
#include <string>

#include "hwcpipe.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/strings/split_string.h"

namespace {
std::string empty_counter_name;
const std::string& CounterNameFromId(hwcpipe::GpuCounter id) {
  for (auto& ents : hwcpipe::gpu_counter_names) {
    if (ents.second == id) {
      return ents.first;
    }
  }
  return empty_counter_name;
}

void PrintUsage() {
  LogError(
      "Usage: mali-performance-counters [--trace | --log | --log-continuous] [--help] "
      "[--period=1000] [--wait-for-key] [--counters=Val1,Val2]\n");
  LogError("Options:\n");
  LogError(" --help           Show this message.\n");
  LogError(" --trace          Output counters to tracing.\n");
  LogError(" --log            Print counters once (in CSV format), then stop.\n");
  LogError(" --log-continuous Repeatedly print counters in CSV format.\n");
  LogError(" --period         Time before first log and between logs, in milliseconds.\n");
  LogError(" --wait-for-key   Wait for a key to be pressed before sampling.\n");
  LogError(
      " --counters       A comma-separated list of counters take from the list below. By default, "
      "all counters are output.\n");
  LogError("Supported/default counter list:\n");
  hwcpipe::HWCPipe pipe;

  auto supported_counters = pipe.gpu_profiler()->supported_counters();
  for (auto& counter : supported_counters) {
    auto info = hwcpipe::gpu_counter_info.find(counter);
    LogError("%s - %s - %s\n", CounterNameFromId(counter).c_str(), info->second.desc.c_str(),
             info->second.unit.c_str());
  }
}
}  // namespace

int CapturePerformanceCounters(fxl::CommandLine command_line) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  loop.StartThread("trace-thread");
  trace::TraceProviderWithFdio provider(loop.dispatcher());

  if (command_line.HasOption("help")) {
    PrintUsage();
    return 0;
  }

  std::string period = command_line.GetOptionValueWithDefault("period", "1000");

  char* endptr;
  int period_int = strtol(period.c_str(), &endptr, 10);
  if (!endptr || *endptr != '\0') {
    LogError("Invalid period value of %s\n", period.c_str());
    PrintUsage();
    return 1;
  }

  bool log_continuous = command_line.HasOption("log-continuous");
  bool log_once = command_line.HasOption("log");
  bool trace = command_line.HasOption("trace");

  uint32_t option_count = log_continuous + log_once + trace;
  if (option_count != 1) {
    LogError("Must specify one of --trace, --log, or --log-continuous\n");
    PrintUsage();
    return 1;
  }

  bool infinite = log_continuous || trace;

  hwcpipe::HWCPipe pipe;

  auto enabled_counters = pipe.gpu_profiler()->supported_counters();

  std::string enabled_counter_string;
  if (command_line.GetOptionValue("counters", &enabled_counter_string)) {
    enabled_counters = hwcpipe::GpuCounterSet();
    auto split =
        fxl::SplitStringCopy(enabled_counter_string, ",", fxl::kKeepWhitespace, fxl::kSplitWantAll);
    for (auto& string : split) {
      auto it = hwcpipe::gpu_counter_names.find(string);
      if (it == hwcpipe::gpu_counter_names.end()) {
        LogError("Invalid counter name \"%s\"\n", string.c_str());
        PrintUsage();
        return 1;
      }
      enabled_counters.insert(it->second);
    }
  }

  pipe.set_enabled_gpu_counters(enabled_counters);

  bool csv = log_once || log_continuous;
  if (csv) {
    Log("Counter,Time difference (nanoseconds),Count\n");
    FlushLog(false);
  }
  if (trace) {
    Log("Outputting traces\n");
    FlushLog(false);
  }
  try {
    pipe.run();
    uint64_t last_timestamp = pipe.gpu_profiler()->timestamp();
    while (true) {
      if (command_line.HasOption("wait-for-key")) {
        Log("Waiting for a key\n");
        FlushLog(false);
        getchar();
      } else {
        zx::nanosleep(zx::deadline_after(zx::msec(period_int)));
      }

      auto measurements = pipe.sample();
      uint64_t this_timestamp = pipe.gpu_profiler()->timestamp();
      if (log_once || log_continuous) {
        for (auto& ctr : *measurements.gpu) {
          std::string name = CounterNameFromId(ctr.first);
          Log("%s,%ld,%d\n", name.c_str(), this_timestamp - last_timestamp,
              ctr.second.get<uint32_t>());
        }
        FlushLog(false);
      } else if (trace) {
        for (auto& ctr : *measurements.gpu) {
          TRACE_COUNTER("gfx", CounterNameFromId(ctr.first).c_str(), static_cast<int>(ctr.first),
                        "value", ctr.second.get<uint32_t>());
        }
        TRACE_COUNTER("gfx", "time_difference", 0, "value", this_timestamp - last_timestamp);
      }
      last_timestamp = this_timestamp;
      if (!infinite) {
        break;
      }
    }
  } catch (const std::runtime_error& e) {
    LogError("Runtime error from mali profiler: %s:", e.what());
    return 1;
  }

  return 0;
}
