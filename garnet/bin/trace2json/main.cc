// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include <iostream>
#include <map>
#include <set>

#include "garnet/bin/trace2json/convert.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"

namespace {

const char kHelp[] = "help";
const char kInputFile[] = "input-file";
const char kOutputFile[] = "output-file";
const char kCompressedInput[] = "compressed-input";
const char kCompressedOutput[] = "compressed-output";

std::set<std::string> kKnownOptions = {
    kHelp, kInputFile, kOutputFile, kCompressedInput, kCompressedOutput,
};

bool ParseBooleanOption(const fxl::CommandLine& command_line, const char* arg_name,
                        bool* out_value) {
  if (command_line.HasOption(std::string_view(arg_name))) {
    std::string arg_value;
    command_line.GetOptionValue(std::string_view(arg_name), &arg_value);
    if (arg_value == "" || arg_value == "true") {
      *out_value = true;
    } else if (arg_value == "false") {
      *out_value = false;
    } else {
      FX_LOGS(ERROR) << "Bad value for --" << arg_name << ", pass true or false";
      return false;
    }
  }
  return true;
}

void PrintHelpMessage() {
  std::map<std::string, std::string> options = {
      {"help", "Print this help message."},
      {"input-file=[]",
       "Read trace from the specified file. If no file is specified, the input "
       "is read from stdin."},
      {"output-file=[]",
       "Write the converted trace to the specified file. If no file is "
       "specified, the output is written to stdout."},
      {"compressed-input=[false]", "If true, the input is first gzip-decompressed."},
      {"compressed-output=[false]",
       "If true, the output is gzip-compressed. Writing compressed output to "
       "stdout is not supported, so output-file must be specified."},
  };

  std::cerr
      << "trace2json [options]: Convert a trace from fxt (Fuchsia trace format) to json (Chrome "
         "trace format)."
      << std::endl
      << "Fuchsia trace format: "
         "https://fuchsia.googlesource.com/fuchsia/+/HEAD/docs/development/tracing/"
         "trace-format/"
      << std::endl
      << "Chrome trace format: "
         "https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/edit"
      << std::endl;
  for (const auto& option : options) {
    std::cerr << "  --" << option.first << ": " << option.second << std::endl;
  }
}

}  // namespace

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    return 1;
  }

  if (command_line.HasOption(kHelp)) {
    PrintHelpMessage();
    return 0;
  }

  bool invalid_options = false;
  for (const auto& option : command_line.options()) {
    if (kKnownOptions.count(option.name) == 0) {
      FX_LOGS(ERROR) << "Unknown option: " << option.name;
      invalid_options = true;
    }
  }

  if (command_line.positional_args().size() > 0) {
    FX_LOGS(ERROR) << "Unexpected positional arg";
    invalid_options = true;
  }

  if (invalid_options) {
    PrintHelpMessage();
    return 1;
  }

  ConvertSettings settings;
  if (command_line.HasOption(kInputFile)) {
    command_line.GetOptionValue(kInputFile, &settings.input_file_name);
  }
  if (command_line.HasOption(kOutputFile)) {
    command_line.GetOptionValue(kOutputFile, &settings.output_file_name);
  }
  if (!ParseBooleanOption(command_line, kCompressedInput, &settings.compressed_input)) {
    PrintHelpMessage();
    return 1;
  }
  if (!ParseBooleanOption(command_line, kCompressedOutput, &settings.compressed_output)) {
    PrintHelpMessage();
    return 1;
  }

  if (!ConvertTrace(settings)) {
    return 1;
  }

  return 0;
}
