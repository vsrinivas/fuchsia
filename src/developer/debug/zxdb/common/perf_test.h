// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_COMMON_PERF_TEST_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_COMMON_PERF_TEST_H_

#include <lib/zx/time.h>

#include <string>

#include "src/lib/fxl/macros.h"

namespace zxdb {

// Initializes and finalizes the perf log. These functions should be called at
// the beginning and end (respectively) of running all the performance tests.
// The init function returns true on success.
bool InitPerfLog(const std::string& log_path);
void FinalizePerfLog();

// Writes to the perf result log the given 'value' resulting from the named
// 'test'. The units are to aid in reading the log by people.
void LogPerfResult(const char* test_suite_name, const char* test_name, double value,
                   const char* units);

// Automates calling LogPerfResult for the common case where you want
// to measure the time that something took. Call Done() when the test
// is complete if you do extra work after the test or there are stack
// objects with potentially expensive constructors. Otherwise, this
// class with automatically log on destruction.
class PerfTimeLogger {
 public:
  PerfTimeLogger(const char* test_suite_name, const char* test_name);
  ~PerfTimeLogger();

  void Done();

 private:
  bool logged_ = false;
  std::string test_suite_name_;
  std::string test_name_;
  zx::time start_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PerfTimeLogger);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_COMMON_PERF_TEST_H_
