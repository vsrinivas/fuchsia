// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings_command_line.h>

#include "garnet/bin/trace2json/convert.h"

namespace {

const char kInputFile[] = "input-file";
const char kOutputFile[] = "output-file";
const char kMagicCheck[] = "perform-magic-check";
const char kCompressedInput[] = "compressed-input";
const char kCompressedOutput[] = "compressed-output";

bool ParseBooleanOption(const fxl::CommandLine& command_line, const char* arg_name,
                        bool* out_value) {
  if (command_line.HasOption(fxl::StringView(arg_name))) {
    std::string arg_value;
    command_line.GetOptionValue(fxl::StringView(arg_name), &arg_value);
    if (arg_value == "" || arg_value == "true") {
      *out_value = true;
    } else if (arg_value == "false") {
      *out_value = false;
    } else {
      FXL_LOG(ERROR) << "Bad value for --" << arg_name << ", pass true or false";
      return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
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
    return 1;
  }
  if (!ParseBooleanOption(command_line, kCompressedOutput, &settings.compressed_output)) {
    return 1;
  }
  if (!ParseBooleanOption(command_line, kMagicCheck, &settings.perform_magic_check)) {
    return 1;
  }

  if (!ConvertTrace(settings)) {
    return 1;
  }

  return 0;
}
