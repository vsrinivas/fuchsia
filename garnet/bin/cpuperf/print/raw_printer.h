// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CPUPERF_PRINT_RAW_PRINTER_H_
#define GARNET_BIN_CPUPERF_PRINT_RAW_PRINTER_H_

#include <cstdio>
#include <memory>
#include <string>

#include <src/lib/fxl/macros.h>

#include "garnet/bin/cpuperf/session_result_spec.h"
#include "garnet/lib/perfmon/records.h"

namespace cpuperf {

class RawPrinter {
 public:
  struct Config {
    // If "" then output goes to stdout.
    std::string output_file_name;
  };

  static bool Create(
      const SessionResultSpec* session_result_spec, const Config& config,
      std::unique_ptr<RawPrinter>* out_printer);

  ~RawPrinter();

  // Raw-print the trace(s).
  // Returns the number of records processed.
  uint64_t PrintFiles();

 private:
  RawPrinter(FILE* output, const SessionResultSpec* session_result_spec,
             const Config& config);

  void Printf(const char* format, ...);
  uint64_t PrintOneTrace(uint32_t iter_num);

  void PrintHeader(const perfmon::SampleRecord& record);
  void PrintTimeRecord(const perfmon::SampleRecord& record);
  void PrintTickRecord(const perfmon::SampleRecord& record);
  void PrintCountRecord(const perfmon::SampleRecord& record);
  void PrintValueRecord(const perfmon::SampleRecord& record);
  void PrintPcRecord(const perfmon::SampleRecord& record);
  void PrintLastBranchRecord(const perfmon::SampleRecord& record);

  FILE* const out_file_;
  const SessionResultSpec* const session_result_spec_;
  const Config config_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RawPrinter);
};

}  // namespace cpuperf

#endif  // GARNET_BIN_CPUPERF_PRINT_RAW_PRINTER_H_
