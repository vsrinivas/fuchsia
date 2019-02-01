// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "third_party/simple-pt/call-printer.h"

#include "printer_config.h"
#include "raw_printer.h"

namespace intel_processor_trace {

using CallPrinter = simple_pt::CallPrinter;

struct CommandLineSettings {
  RawPrinter::Config ToRawPrinterConfig() const;
  CallPrinter::Config ToCallPrinterConfig() const;

  OutputFormat output_format = OutputFormat::kCalls;

  // If "" then output goes to the default location (typically stdout).
  std::string output_file_name;

  OutputView view = OutputView::kCpu;

  bool abstime = false;
  bool report_lost_mtc_cyc = false;
  bool dump_pc = false;
  bool dump_insn = false;

  // The id field for chrome trace output.
  // For cpu traces this is the cpu number.
  static constexpr uint32_t kIdUnset = 0xffffffff;
  uint32_t id = kIdUnset;
};

}  // namespace intel_processor_trace
