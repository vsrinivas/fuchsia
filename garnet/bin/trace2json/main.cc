// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <fstream>
#include <iostream>
#include <vector>

#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings_command_line.h>
#include <trace-reader/reader.h>

#include "garnet/bin/trace2json/trace_parser.h"
#include "garnet/lib/trace_converters/chromium_exporter.h"

namespace {

const char kInputFile[] = "input-file";
const char kOutputFile[] = "output-file";

}  // namespace

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    return 1;
  }

  std::unique_ptr<std::ifstream> input_file_stream;
  std::unique_ptr<std::ofstream> output_file_stream;

  std::istream* in_stream = &std::cin;
  std::ostream* out_stream = &std::cout;

  if (command_line.HasOption(kInputFile)) {
    std::string input_file_name;
    command_line.GetOptionValue(kInputFile, &input_file_name);
    input_file_stream = std::make_unique<std::ifstream>(
        input_file_name.c_str(), std::ios_base::in | std::ios_base::binary);
    in_stream = static_cast<std::istream*>(input_file_stream.get());
  }

  if (command_line.HasOption(kOutputFile)) {
    std::string output_file_name;
    command_line.GetOptionValue(kOutputFile, &output_file_name);
    output_file_stream = std::make_unique<std::ofstream>(
        output_file_name.c_str(), std::ios_base::out | std::ios_base::trunc);
    out_stream = static_cast<std::ostream*>(output_file_stream.get());
  }

  tracing::FuchsiaTraceParser parser(out_stream);
  if (!parser.Parse(in_stream)) {
    return 1;
  }

  return 0;
}
