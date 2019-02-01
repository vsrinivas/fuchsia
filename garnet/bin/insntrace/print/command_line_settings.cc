// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_line_settings.h"

namespace intel_processor_trace {

RawPrinter::Config CommandLineSettings::ToRawPrinterConfig() const {
  RawPrinter::Config config;
  config.output_file_name = output_file_name;
  return config;
};

CallPrinter::Config CommandLineSettings::ToCallPrinterConfig() const {
  CallPrinter::Config config;
  config.output_file_name = output_file_name;
  config.abstime = abstime;
  config.report_lost_mtc_cyc = report_lost_mtc_cyc;
  config.dump_pc = dump_pc;
  config.dump_insn = dump_insn;
  return config;
};

}  // namespace intel_processor_trace
