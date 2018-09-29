// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CPUPERF_PRINT_PRINTER_CONFIG_H_
#define GARNET_BIN_CPUPERF_PRINT_PRINTER_CONFIG_H_

#include <string>

#include "raw_printer.h"

namespace cpuperf {

// Various kinds of output format.
enum class OutputFormat {
  // Raw format. Prints data for each instruction.
  kRaw,
};

struct PrinterConfig {
  RawPrinter::Config ToRawPrinterConfig() const;

  OutputFormat output_format = OutputFormat::kRaw;

  // If "" then output goes to the default location (typically stdout).
  std::string output_file_name;
};

}  // namespace cpuperf

#endif  // GARNET_BIN_CPUPERF_PRINT_PRINTER_CONFIG_H_
