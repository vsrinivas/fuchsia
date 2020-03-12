// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <chrono>
#include <string>

#include "garnet/bin/cpuperf/session_result_spec.h"
#include "printer_config.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

static constexpr char kUsageString[] =
    "cpuperf_print [options]\n"
    "\n"
    "These options are required:\n"
    "--session=FILE      Session result spec file\n"
    "                    Trace files are assumed to live in the same directory\n"
    "\n"
    "The remaining options are optional.\n"
    "\n"
    "General output options:\n"
    "--output-format=raw\n"
    "                    Default is \"raw\"\n"
    "--output-file=PATH\n"
    "                    For raw the default is stdout.\n"
    "\n"
    "Logging options:\n"
    "  --quiet[=LEVEL]   Set quietness level (opposite of verbose)\n"
    "  --verbose[=LEVEL] Set debug verbosity level\n"
    "  --log-file=FILE   Write log output to FILE.\n"
    "Quiet supersedes verbose if both are specified.\n"
    "Defined log levels:\n"
    "-n - verbosity level n\n"
    " 0 - INFO - this is the default level\n"
    " 1 - WARNING\n"
    " 2 - ERROR\n"
    " 3 - FATAL\n";

static void PrintUsage(FILE* f) { fputs(kUsageString, f); }

static bool ParseArgv(const fxl::CommandLine& cl, std::string* session_result_spec_path,
                      cpuperf::SessionResultSpec* out_session_result_spec,
                      cpuperf::PrinterConfig* out_printer_config) {
  std::string arg;

  if (!cl.GetOptionValue("session", session_result_spec_path)) {
    FXL_LOG(ERROR) << "Missing --session argument";
    return false;
  }
  std::string content;
  if (!files::ReadFileToString(*session_result_spec_path, &content)) {
    FXL_LOG(ERROR) << "Can't read " << *session_result_spec_path;
    return false;
  }
  if (!DecodeSessionResultSpec(content, out_session_result_spec)) {
    return false;
  }

  if (cl.GetOptionValue("output-format", &arg)) {
    if (arg == "raw") {
      out_printer_config->output_format = cpuperf::OutputFormat::kRaw;
    } else {
      FXL_LOG(ERROR) << "Bad value for --output-format: " << arg;
      return false;
    }
  }

  if (cl.GetOptionValue("output-file", &arg)) {
    out_printer_config->output_file_name = arg;
  }

  const std::vector<std::string>& positional_args = cl.positional_args();
  if (positional_args.size() > 0) {
    FXL_LOG(ERROR) << "No positional parameters";
    return false;
  }

  return true;
}

int main(int argc, char** argv) {
  fxl::CommandLine cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(cl))
    return EXIT_FAILURE;

  if (cl.HasOption("help")) {
    PrintUsage(stdout);
    return EXIT_SUCCESS;
  }

  std::string session_result_spec_path;
  cpuperf::SessionResultSpec session_result_spec;
  cpuperf::PrinterConfig printer_config;
  if (!ParseArgv(cl, &session_result_spec_path, &session_result_spec, &printer_config)) {
    return EXIT_FAILURE;
  }

  // Modify the recorded output path prefix to point to where we found the
  // session result spec. The directory currently recorded is probably for
  // the target.
  std::string spec_directory = files::GetDirectoryName(session_result_spec_path);
  if (spec_directory == "") {
    spec_directory = ".";
  }
  std::string path_prefix_basename = files::GetBaseName(session_result_spec.output_path_prefix);
  session_result_spec.output_path_prefix = spec_directory + "/" + path_prefix_basename;

  const auto start_time = std::chrono::steady_clock::now();

  if (!session_result_spec.config_name.empty()) {
    FXL_LOG(INFO) << "Config: " << session_result_spec.config_name;
  } else {
    FXL_LOG(INFO) << "Config: unnamed";
  }
  FXL_LOG(INFO) << session_result_spec.num_iterations << " iteration(s), "
                << session_result_spec.num_traces << " trace(s)";
  FXL_LOG(INFO) << "Output path prefix: " << session_result_spec.output_path_prefix;

  uint64_t total_records;
  if (printer_config.output_format == cpuperf::OutputFormat::kRaw) {
    std::unique_ptr<cpuperf::RawPrinter> printer;
    if (!cpuperf::RawPrinter::Create(&session_result_spec, printer_config.ToRawPrinterConfig(),
                                     &printer)) {
      return EXIT_FAILURE;
    }
    total_records = printer->PrintFiles();
  } else {
    FXL_LOG(ERROR) << "Invalid output format\n";
    return EXIT_FAILURE;
  }

  const auto& delta = (std::chrono::steady_clock::now() - start_time);
  int64_t seconds = std::chrono::duration_cast<std::chrono::seconds>(delta).count();
  int milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(delta).count() % 1000;
  FXL_LOG(INFO) << fxl::StringPrintf("%" PRIu64 " records processed in %" PRId64 ".%03d seconds\n",
                                     total_records, seconds, milliseconds);

  return EXIT_SUCCESS;
}
