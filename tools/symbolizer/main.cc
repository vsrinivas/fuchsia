// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>
#include <iostream>

#include "src/lib/fxl/strings/trim.h"
#include "tools/symbolizer/command_line_options.h"
#include "tools/symbolizer/log_parser.h"
#include "tools/symbolizer/printer.h"
#include "tools/symbolizer/symbolizer_impl.h"

namespace symbolizer {

int Main(int argc, const char* argv[]) {
  CommandLineOptions options;

  if (const Error error = ParseCommandLine(argc, argv, &options); !error.empty()) {
    // Sometimes the error just has too many "\n" at the end.
    std::cerr << fxl::TrimString(error, "\n") << std::endl;
    return EXIT_FAILURE;
  }

  Printer printer(std::cout);
  SymbolizerImpl symbolizer(&printer, options);
  LogParser parser(std::cin, &printer, &symbolizer);

  while (parser.ProcessOneLine()) {
    // until the eof in the input.
  }

  return EXIT_SUCCESS;
}

}  // namespace symbolizer

int main(int argc, const char* argv[]) { return symbolizer::Main(argc, argv); }
