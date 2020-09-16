// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_SYMBOLIZER_LOG_PARSER_H_
#define TOOLS_SYMBOLIZER_LOG_PARSER_H_

#include <iostream>
#include <string_view>

#include "tools/symbolizer/printer.h"
#include "tools/symbolizer/symbolizer.h"

namespace symbolizer {

// This is the "main class" of the symbolizer. A LogParser takes an input stream, reads lines and
// writes to an output stream. If a line contains symbolizer markups, i.e., {{{...}}}, the
// LogParser will parse its content and pass to the real symbolizer. The symbolizer markup format
// is documented in //docs/reference/kernel/symbolizer_markup.md.
//
// For simplicity, this implementation has the following assumptions/limitations.
// 1. Interleaved stack traces are not supported. There can be at most 1 stack trace at a time.
// 2. Log will presume its order. When a symbolizer markup is being processed, e.g., downloading the
//    symbol file, the output will stall, even if the next line contains no markup.
// 3. Only one markup per line is supported.
class LogParser {
 public:
  // Initializes the LogParser. All of the parameters must outlive this LogParser.
  LogParser(std::istream& input, Printer* printer, Symbolizer* symbolizer)
      : input_(input), printer_(printer), symbolizer_(symbolizer) {}

  // Reads the next line from the input, sends it to the symbolizer or writes to the output.
  // Returns false if there's no more line in the input.
  bool ProcessOneLine();

 private:
  // Processes one markup. Returns whether the markup could be processed successfully.
  bool ProcessMarkup(std::string_view markup);

  std::istream& input_;
  Printer* printer_;
  Symbolizer* symbolizer_;
};

}  // namespace symbolizer

#endif  // TOOLS_SYMBOLIZER_LOG_PARSER_H_
