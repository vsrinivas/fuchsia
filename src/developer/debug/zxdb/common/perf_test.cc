// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/perf_test.h"

#include <stdio.h>

#include "src/lib/fxl/logging.h"

namespace zxdb {

namespace {

FILE* perf_log_file = nullptr;
bool written_perf_line = false;

}  // namespace

bool InitPerfLog(const std::string& log_file) {
  if (perf_log_file) {
    // Trying to initialize twice.
    FXL_NOTREACHED();
    return false;
  }

  if (!(perf_log_file = fopen(log_file.c_str(), "w")))
    return false;

  fprintf(perf_log_file, "{\n");
  return true;
}

void FinalizePerfLog() {
  if (!perf_log_file) {
    // Trying to cleanup without initializing.
    FXL_NOTREACHED();
    return;
  }
  fprintf(perf_log_file, "\n}\n");
  fclose(perf_log_file);
  perf_log_file = nullptr;
}

void LogPerfResult(const char* test_suite_name, const char* test_name, double value,
                   const char* units) {
  if (!perf_log_file) {
    FXL_NOTREACHED();
    return;
  }

  // Format: //zircon/system/ulib/perftest/performance-results-schema.json
  // Example line:
  //  {"label":"Vmo/CloneWrite/10000kbytes.close",
  //   "test_suite":"fuchsia.microbenchmarks",
  //   "unit":"nanoseconds",
  //   "values":[2346.961749]}

  // Add trailing comma for previous item when necessary.
  if (written_perf_line)
    fprintf(perf_log_file, ",\n");
  written_perf_line = true;

  fprintf(perf_log_file, R"({"label":"%s", "test_suite":"%s", "unit":"%s", "values":[%g]})",
          test_name, test_suite_name, units, value);
  fflush(stdout);
}

PerfTimeLogger::PerfTimeLogger(const char* test_suite_name, const char* test_name)
    : test_suite_name_(test_suite_name), test_name_(test_name) {
  timer_.Start();
}

PerfTimeLogger::~PerfTimeLogger() {
  if (!logged_)
    Done();
}

void PerfTimeLogger::Done() {
  // Use a floating-point millisecond value because it is more
  // intuitive than microseconds and we want more precision than
  // integer milliseconds
  LogPerfResult(test_suite_name_.c_str(), test_name_.c_str(), timer_.Elapsed().ToMillisecondsF(),
                "ms");
  logged_ = true;
}

}  // namespace zxdb
